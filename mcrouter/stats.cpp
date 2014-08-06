/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "stats.h"

#include <dirent.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <mcrouter/config.h>

#include <folly/Range.h>
#include <folly/json.h>

#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/_router.h"
#include "mcrouter/dynamic_stats.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/StatsReply.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/proxy.h"

/**                             .__
 * __  _  _______ _______  ____ |__| ____    ____
 * \ \/ \/ /\__  \\_  __ \/    \|  |/    \  / ___\
 *  \     /  / __ \|  | \/   |  \  |   |  \/ /_/  >
 *   \/\_/  (____  /__|  |___|  /__|___|  /\___  /
 *               \/           \/        \//_____/
 *
 * Read the following code with proper care for life and limb.
 */

namespace facebook { namespace memcache { namespace mcrouter {

// This is a subset of what's in proc(5).
struct proc_stat_data_t {
  unsigned long num_minor_faults;
  unsigned long num_major_faults;
  double user_time_sec;
  double system_time_sec;
  unsigned long vsize;
  unsigned long rss;
};

double stats_rate_value(proxy_t* proxy, int idx) {
  const stat_t* stat = &proxy->stats[idx];
  double rate = 0;

  if (proxy->num_bins_used != 0) {
    if (stat->aggregate) {
      mcrouter_t* router = proxy->router;
      uint64_t num = 0;
      for (auto& pr : router->proxy_threads) {
        num += pr->proxy->stats_num_within_window[idx];
      }
      rate = (double)num / (proxy->num_bins_used *
                            MOVING_AVERAGE_BIN_SIZE_IN_SECOND);
    } else {
      rate = (double)proxy->stats_num_within_window[idx] /
        (proxy->num_bins_used * MOVING_AVERAGE_BIN_SIZE_IN_SECOND);
    }
  }

  return rate;
}

static std::string rate_stat_to_str(proxy_t * proxy,
                                    int idx) {
  return folly::stringPrintf("%g", stats_rate_value(proxy, idx));
}

/**
 * Write a stat into a buffer.
 *
 * @param stat_t* stat the stat to write
 * @param char* buf the already allocated buffer to write into
 * @param void* ptr the ptr to the structure that has the stat to be written
 *
 * @eturn the length of the string written, excluding terminator
 */
static std::string stat_to_str(const stat_t* stat, void *ptr) {
  switch (stat->type) {
    case stat_string_fn:
      return stat->data.string_fn(ptr);
    case stat_string:
      return stat->data.string;
    case stat_uint64:
      return folly::to<std::string>(stat->data.uint64);
    case stat_int64:
      return folly::to<std::string>(stat->data.int64);
    case stat_double:
      return folly::stringPrintf("%g", stat->data.dbl);
    default:
      LOG(ERROR) << "unknown stat type " << stat->type << " (" <<
                    stat->name << ")";
      return "";
  }
}

static std::string stat_get_rusage(void* ptr) {
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  return folly::stringPrintf(
      "{ user_time: %ld.%06ld, "
      "system_time: %ld.%06ld, "
      "max_rss: %ld, "
      "minor_page_flt: %ld, "
      "major_page_flt: %ld, "
      "in_block: %ld, "
      "out_block: %ld, "
      "num_signals: %ld, "
      "voluntary_ctx_switches: %ld, "
      "involuntary_ctx_switches: %ld }",
      ru.ru_utime.tv_sec, (long)ru.ru_utime.tv_usec,
      ru.ru_stime.tv_sec, (long)ru.ru_stime.tv_usec,
      ru.ru_maxrss, ru.ru_minflt, ru.ru_majflt,
      ru.ru_inblock, ru.ru_oublock, ru.ru_nsignals,
      ru.ru_nvcsw, ru.ru_nivcsw);
}

// define all data structs for stats here [median, percentile, etc]

void init_stats(stat_t *stats) {
#define STAT(_name, _type, _aggregate, _data_assignment)                \
  {                                                                     \
    stat_t& s = stats[_name##_stat];                                    \
    s.name = #_name;                                                    \
    s.group = GROUP;                                                    \
    s.type = _type;                                                     \
    s.aggregate = _aggregate;                                           \
    s.data _data_assignment;                                            \
  }
#define STUI(name, value, agg) STAT(name, stat_uint64, agg, .uint64=value)
#define STUIR(name, value, agg) STAT(name, stat_uint64, agg, .uint64=value)
#define STSI(name, value, agg) STAT(name, stat_int64, agg, .int64=value)
#define STSS(name, value, agg) STAT(name, stat_string, agg, \
                                    .string=(char*)value)
#include "stat_list.h"
#undef STAT
#undef STUI
#undef STUIR
#undef STSI
#undef STSS
}

uint64_t stat_get_config_age(const proxy_t *proxy, uint64_t now) {
  uint64_t lct = proxy->stats[config_last_success_stat].data.uint64;
  return now - lct;
}

// Returns 0 on success, -1 on failure.  In either case, all fields of
// *data will be initialized to something.
static int get_proc_stat(pid_t pid, proc_stat_data_t *data) {
  data->num_minor_faults = 0;
  data->num_major_faults = 0;
  data->user_time_sec = 0.0;
  data->system_time_sec = 0.0;
  data->vsize = 0;
  data->rss = 0;

  char stat_path[32];
  snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);

  FILE *stat_file = fopen(stat_path, "r");
  if (stat_file == nullptr) {
    LOG(ERROR) << "Can't open process status information file: " <<
                  stat_path << ": " << strerror(errno);
    return -1;
  }

  // Note: the field list in proc(5) on my dev machine was incorrect.
  // I have confirmed that this is correct:
  //
  // http://manpages.ubuntu.com/manpages/lucid/man5/proc.5.html
  //
  // We only report out a few of the fields from the stat file, but it
  // should be easy to add more later if they are desired.

  long rss_pages;
  unsigned long utime_ticks;
  unsigned long stime_ticks;

  int count = fscanf(stat_file,
                     "%*d (%*[^)]) %*c %*d %*d %*d %*d %*d %*u %lu "
                     "%*u %lu %*u %lu %lu %*d %*d %*d %*d %*d "
                     "%*d %*u %lu %ld" /* and there's more */,
                     &data->num_minor_faults, &data->num_major_faults,
                     &utime_ticks, &stime_ticks,
                     &data->vsize, &rss_pages);
  fclose(stat_file);

  if (count != 6) {
    return -1;
  }

  data->user_time_sec = ((double) utime_ticks) / sysconf(_SC_CLK_TCK);
  data->system_time_sec = ((double) stime_ticks) / sysconf(_SC_CLK_TCK);

  // rss is documented to be signed, but since negative numbers are
  // nonsensical, and nothing else is in pages, we clamp it and
  // convert to bytes here.

  data->rss =
    rss_pages < 0 ? 0ul : (unsigned long) (rss_pages * sysconf(_SC_PAGESIZE));

  return 0;
}

/**
 * Sets openFD to number of opened file descriptors in current process
 * on success, otherwise the value of openFD is not changed
 *
 * @return true on success, false on failure
 */
static bool getOpenFDCount(size_t& openFD) {
  char procfd_path[32];
  snprintf(procfd_path, 32, "/proc/%d/fd", getpid());

  DIR *dir = opendir(procfd_path);
  if (dir == nullptr) {
    LOG(ERROR) << "Can't open process file descriptor directory: " <<
                  procfd_path << ": " << strerror(errno);
    return false;
  }
  size_t res = 0;
  while (readdir(dir) != nullptr) {
    ++res;
  }
  closedir(dir);
  // subtract 3 to exclude '.', '..' and currently opened dir
  openFD = res - 3;
  return true;
}

void prepare_stats(proxy_t *proxy, stat_t *stats) {
  mcrouter_t *router = proxy->router;
  init_stats(stats);

  if (router->prepare_proxy_server_stats) {
    router->prepare_proxy_server_stats(proxy);
  }

  // Put rtt stats in stats
  stats[rtt_min_stat].data.uint64 = proxy->stats[rtt_min_stat].data.uint64;
  stats[rtt_stat].data.uint64 = proxy->stats[rtt_stat].data.uint64;
  stats[rtt_max_stat].data.uint64 = proxy->stats[rtt_max_stat].data.uint64;

  uint64_t total_mcc_txbuf_reqs = 0;
  uint64_t total_mcc_waiting_replies = 0;
  for (auto& pr : router->proxy_threads) {
    auto cnt = pr->proxy->destinationMap->getOutstandingRequestStats();
    total_mcc_txbuf_reqs += cnt.first;
    total_mcc_waiting_replies += cnt.second;
  }

  stat_set_uint64(proxy, mcc_txbuf_reqs_stat, total_mcc_txbuf_reqs);
  stat_set_uint64(proxy, mcc_waiting_replies_stat, total_mcc_waiting_replies);

  stats[commandargs_stat].data.string = router->command_args;

  uint64_t now = time(nullptr);
  stats[time_stat].data.uint64 = now;

  uint64_t start_time = router->start_time;
  stats[start_time_stat].data.uint64 = start_time;
  stats[uptime_stat].data.uint64 = now - start_time;

  stats[config_age_stat].data.uint64 = stat_get_config_age(proxy, now);
  stats[config_last_success_stat].data.uint64 =
    proxy->stats[config_last_success_stat].data.uint64;
  stats[config_last_attempt_stat].data.uint64 = router->last_config_attempt;
  stats[config_failures_stat].data.uint64 = router->config_failures;

  stats[child_pid_stat].data.int64 = getpid();
  stats[parent_pid_stat].data.int64 = getppid();

  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  stats[rusage_user_stat].data.dbl =
    ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1000000.0;

  stats[rusage_system_stat].data.dbl =
    ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1000000.0;

  proc_stat_data_t ps_data;
  get_proc_stat(getpid(), &ps_data);
  stats[ps_num_minor_faults_stat].data.uint64 = ps_data.num_minor_faults;
  stats[ps_num_major_faults_stat].data.uint64 = ps_data.num_major_faults;
  stats[ps_user_time_sec_stat].data.dbl = ps_data.user_time_sec;
  stats[ps_system_time_sec_stat].data.dbl = ps_data.system_time_sec;
  stats[ps_rss_stat].data.uint64 = ps_data.rss;
  stats[ps_vsize_stat].data.uint64 = ps_data.vsize;
  size_t open_fd = 0;
  getOpenFDCount(open_fd);
  stats[ps_open_fd_stat].data.uint64 = open_fd;

  stats[fibers_allocated_stat].data.uint64 = 0;
  stats[fibers_pool_size_stat].data.uint64 = 0;
  stats[fibers_stack_high_watermark_stat].data.uint64 = 0;
  for (const auto& proxy_thread : router->proxy_threads) {
    auto pr = proxy_thread->proxy;
    stats[fibers_allocated_stat].data.uint64 +=
      pr->fiberManager.fibersAllocated();
    stats[fibers_pool_size_stat].data.uint64 +=
      pr->fiberManager.fibersPoolSize();
    stats[fibers_stack_high_watermark_stat].data.uint64 =
      std::max(stats[fibers_stack_high_watermark_stat].data.uint64,
               pr->fiberManager.stackHighWatermark());
    stats[duration_us_stat].data.dbl += pr->durationUs.getCurrentValue();
  }
  if (!router->proxy_threads.empty()) {
    stats[duration_us_stat].data.dbl /= router->proxy_threads.size();
  }
#ifndef FBCODE_OPT_BUILD
  stats[mc_msg_num_outstanding_stat].data.uint64 =
    mc_msg_num_outstanding();
#endif

  for (int i = 0; i < num_stats; i++) {
    if (stats[i].aggregate && !(stats[i].group & rate_stats)) {
      for (auto& proxy_thread : router->proxy_threads) {
        auto pr = proxy_thread->proxy;
        if (stats[i].type == stat_uint64) {
          stats[i].data.uint64 += pr->stats[i].data.uint64;
        } else if (stats[i].type == stat_int64) {
          stats[i].data.int64 += pr->stats[i].data.int64;
        } else if (stats[i].type == stat_double) {
          stats[i].data.dbl += pr->stats[i].data.dbl;
        } else {
          LOG(FATAL) << "you can't aggregate non-numerical stats!";
        }
      }
    }
  }

  // num_servers_down is the inverse of num_servers_up wrt num_servers
  stats[num_servers_down_stat].data.uint64 =
    stats[num_servers_stat].data.uint64 -
    stats[num_servers_up_stat].data.uint64;
}

void stat_incr(proxy_t *proxy, stat_name_t stat_num, int64_t amount) {
  proxy->stats[stat_num].data.uint64 += amount;
}

void stat_decr(proxy_t *proxy, stat_name_t stat_num, int64_t amount) {
  stat_incr(proxy, stat_num, -amount);
}

// Thread-safe increment of the given counter
void stat_incr_safe(proxy_t *proxy, stat_name_t stat_name) {
  __sync_fetch_and_add(&proxy->stats[stat_name].data.uint64, 1);
}

void stat_decr_safe(proxy_t *proxy, stat_name_t stat_name) {
  __sync_fetch_and_add(&proxy->stats[stat_name].data.uint64, -1);
}

void stat_set_uint64(proxy_t *proxy,
                            stat_name_t stat_num,
                            uint64_t value) {
  stat_t* stat = &proxy->stats[stat_num];
  FBI_ASSERT(stat->type == stat_uint64);
  stat->data.uint64 = value;
}

uint64_t stat_get_uint64(proxy_t *proxy, stat_name_t stat_num) {
  stat_t* stat = &proxy->stats[stat_num];
  return stat->data.uint64;
}

static stat_group_t stat_parse_group_str(folly::StringPiece str) {
  if (str == "all") {
    return all_stats;
  } else if (str == "detailed") {
    return detailed_stats;
  } else if (str == "cmd") {
    return cmd_all_stats;
  } else if (str == "cmd-in") {
    return cmd_in_stats;
  } else if (str == "cmd-out") {
    return cmd_out_stats;
  } else if (str == "cmd-error") {
    return cmd_error_stats;
  } else if (str == "ods") {
    return ods_stats;
  } else if (str == "timers") {
    return timer_stats;
  } else if (str == "servers") {
    return server_stats;
  } else if (str == "memory") {
    return memory_stats;
  } else if (str == "count") {
    return count_stats;
  } else if (str.empty()) {
    return mcproxy_stats;
  } else {
    return unknown_stats;
  }
}

/**
 * @param proxy_t proxy
 */
McReply stats_reply(proxy_t* proxy, folly::StringPiece group_str) {
  std::lock_guard<std::mutex> guard(proxy->stats_lock);

  proxy->flushRttStats();

  StatsReply reply;

  if (group_str == "version") {
    reply.addStat("mcrouter-version", MCROUTER_PACKAGE_STRING);
    return reply.getMcReply();
  }

  auto groups = stat_parse_group_str(group_str);
  if (groups == unknown_stats) {
    return McReply(mc_res_client_error, "bad stats command");
  }

  stat_t stats[num_stats];
  init_stats(stats);

  prepare_stats(proxy, stats);

  for (unsigned int ii = 0; ii < num_stats; ii++) {
    stat_t* stat = &stats[ii];
    if (stat->group & groups) {
      if (stat->group & rate_stats) {
        reply.addStat(stat->name, rate_stat_to_str(proxy, ii));
      } else {
        reply.addStat(stat->name, stat_to_str(stat, nullptr));
      }
    }
  }

  if (groups & timer_stats) {
    fb_timer_list_t timer_list = fb_timer_get_all_timers();
    fb_timer_t* timer;
    TAILQ_FOREACH(timer, &timer_list, entry) {
      nstring_t timers[NUM_TIMER_OUTPUT_TYPES];
      fb_timer_to_nstring(timer, timers);
      for (int i = 0; i < NUM_TIMER_OUTPUT_TYPES; ++i) {
        reply.addStat(to<folly::StringPiece>(timer->names[i]),
                      to<folly::StringPiece>(timers[i]));
        free(timers[i].str);
      }
    }
  }

  if (groups & server_stats) {
    std::lock_guard<std::mutex> lg(dynamic_stats_mutex());
    dynamic_stats_list_t dynamic_stat_list = dynamic_stats_get_all();
    dynamic_stat_t* d_stat;
    TAILQ_FOREACH(d_stat, &dynamic_stat_list, entry) {
      reply.addStat(d_stat->stat.name,
                    stat_to_str(&d_stat->stat, d_stat->entity_ptr));
    }
  }

  return reply.getMcReply();
}

}}} // facebook::memcache::mcrouter
