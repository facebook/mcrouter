/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "stats.h"

#include <dirent.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <folly/Conv.h>
#include <folly/json.h>
#include <folly/Range.h>

#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/StatsReply.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/ProxyThread.h"

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

namespace {

char* gStandaloneArgs = nullptr;

const char* clientStateToStr(ProxyDestination::State state) {
  switch (state) {
    case ProxyDestination::State::kUp:
      return "up";
    case ProxyDestination::State::kNew:
      return "new";
    case ProxyDestination::State::kClosed:
      return "closed";
    case ProxyDestination::State::kDown:
      return "down";
    case ProxyDestination::State::kNumStates:
      assert(false);
  }
  return "unknown";
}

struct ServerStat {
  uint64_t results[mc_nres] = {0};
  size_t states[(size_t)ProxyDestination::State::kNumStates] = {0};
  bool isHardTko{false};
  bool isSoftTko{false};
  double sumLatencies{0.0};
  size_t cntLatencies{0};
  size_t pendingRequestsCount{0};
  size_t inflightRequestsCount{0};

  std::string toString() const {
    double avgLatency = cntLatencies == 0 ? 0 : sumLatencies / cntLatencies;
    auto res = folly::format("avg_latency_us:{:.3f}", avgLatency).str();
    folly::format(" pending_reqs:{}", pendingRequestsCount).appendTo(res);
    folly::format(" inflight_reqs:{}", inflightRequestsCount).appendTo(res);
    if (isHardTko) {
      folly::format(" hard_tko; ").appendTo(res);
    } else if (isSoftTko) {
      folly::format(" soft_tko; ").appendTo(res);
    }
    for (size_t i = 0; i < (size_t)ProxyDestination::State::kNumStates; ++i) {
      if (states[i] > 0) {
        auto state = clientStateToStr(static_cast<ProxyDestination::State>(i));
        folly::format(" {}:{}", state, states[i]).appendTo(res);
      }
    }
    bool firstResult = true;
    for (size_t i = 0; i < mc_nres; ++i) {
      if (results[i] > 0) {
        folly::StringPiece result(mc_res_to_string(static_cast<mc_res_t>(i)));
        result.removePrefix("mc_res_");
        folly::format("{} {}:{}", firstResult ? ";" : "", result,
                      results[i]).appendTo(res);
        firstResult = false;
      }
    }
    return res;
  }
};

/**
 * Represents a set of aggregated stats across all destinations in a map.
 */
struct AggregatedDestinationStats {
  // Number of requests pending to be sent over network.
  uint64_t pendingRequests{0};
  // Number of requests been sent over network or already waiting for reply.
  uint64_t inflightRequests{0};
  // Average batches size in form of (num_request, num_batches).
  // This value is based on some subset of most recent requests (each
  // destination maintains its own window).
  // See AsyncMcClient::getBatchingStat() for more details.
  std::pair<uint64_t, uint64_t> batches{0, 0};
};

int get_num_bins_used(const McrouterInstance& router) {
  if (router.opts().num_proxies > 0) {
    const proxy_t* anyProxy = router.getProxy(0);
    if (anyProxy) {
      return anyProxy->num_bins_used;
    }
  }
  return 0;
}

double stats_rate_value(proxy_t* proxy, int idx) {
  const stat_t* stat = &proxy->stats[idx];
  double rate = 0;

  if (proxy->num_bins_used != 0) {
    if (stat->aggregate) {
      rate = stats_aggregate_rate_value(proxy->router(), idx);
    } else {
      rate = (double)proxy->stats_num_within_window[idx] /
        (proxy->num_bins_used * MOVING_AVERAGE_BIN_SIZE_IN_SECOND);
    }
  }

  return rate;
}

}  // anonymous namespace

// This is a subset of what's in proc(5).
struct proc_stat_data_t {
  unsigned long num_minor_faults;
  unsigned long num_major_faults;
  double user_time_sec;
  double system_time_sec;
  unsigned long vsize;
  unsigned long rss;
};

double stats_aggregate_rate_value(const McrouterInstance& router, int idx) {
  double rate = 0;
  int num_bins_used = get_num_bins_used(router);

  if (num_bins_used != 0) {
    uint64_t num = 0;
    for (size_t i = 0; i < router.opts().num_proxies; ++i) {
      num += router.getProxy(i)->stats_num_within_window[idx];
    }
    rate = (double)num / (num_bins_used * MOVING_AVERAGE_BIN_SIZE_IN_SECOND);
  }

  return rate;
}



static std::string rate_stat_to_str(proxy_t * proxy, int idx) {
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

void init_stats(stat_t* stats) {
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

uint64_t stat_get_config_age(const stat_t* stats, uint64_t now) {
  uint64_t lct = stats[config_last_success_stat].data.uint64;
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

void prepare_stats(McrouterInstance& router, stat_t* stats) {
  init_stats(stats);

  AggregatedDestinationStats destStats;
  uint64_t config_last_success = 0;
  for (size_t i = 0; i < router.opts().num_proxies; ++i) {
    auto proxy = router.getProxy(i);
    config_last_success = std::max(config_last_success,
      proxy->stats[config_last_success_stat].data.uint64);
    proxy->destinationMap->foreachDestinationSynced(
      [&destStats](folly::StringPiece, const ProxyDestination& destination) {
        destStats.pendingRequests += destination.getPendingRequestCount();
        destStats.inflightRequests += destination.getInflightRequestCount();
        auto batch = destination.getBatchingStat();
        destStats.batches.first += batch.first;
        destStats.batches.second += batch.second;
      }
    );
  }

  stat_set_uint64(stats, num_suspect_servers_stat,
                  router.tkoTrackerMap().getSuspectServersCount());

  stat_set_uint64(stats, mcc_txbuf_reqs_stat, destStats.pendingRequests);
  stat_set_uint64(stats, mcc_waiting_replies_stat, destStats.inflightRequests);

  double avgBatchSize = 0;
  if (destStats.batches.second != 0) {
    avgBatchSize = destStats.batches.first / (double)destStats.batches.second;
  }
  stats[destination_batch_size_stat].data.dbl = avgBatchSize;

  stats[commandargs_stat].data.string = gStandaloneArgs;

  uint64_t now = time(nullptr);
  stats[time_stat].data.uint64 = now;

  uint64_t start_time = router.startTime();
  stats[start_time_stat].data.uint64 = start_time;
  stats[uptime_stat].data.uint64 = now - start_time;

  stats[config_age_stat].data.uint64 = now - config_last_success;
  stats[config_last_success_stat].data.uint64 = config_last_success;
  stats[config_last_attempt_stat].data.uint64 = router.lastConfigAttempt();
  stats[config_failures_stat].data.uint64 = router.configFailures();

  stats[pid_stat].data.int64 = getpid();
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

  stats[fibers_allocated_stat].data.uint64 = 0;
  stats[fibers_pool_size_stat].data.uint64 = 0;
  stats[fibers_stack_high_watermark_stat].data.uint64 = 0;
  for (size_t i = 0; i < router.opts().num_proxies; ++i) {
    auto pr = router.getProxy(i);
    stats[fibers_allocated_stat].data.uint64 +=
      pr->fiberManager.fibersAllocated();
    stats[fibers_pool_size_stat].data.uint64 +=
      pr->fiberManager.fibersPoolSize();
    stats[fibers_stack_high_watermark_stat].data.uint64 =
      std::max(stats[fibers_stack_high_watermark_stat].data.uint64,
               pr->fiberManager.stackHighWatermark());
    stats[duration_us_stat].data.dbl += pr->durationUs.value();
    stats[client_queue_notify_period_stat].data.dbl += pr->queueNotifyPeriod();
  }
  if (router.opts().num_proxies > 0) {
    stats[duration_us_stat].data.dbl /= router.opts().num_proxies;
    stats[client_queue_notify_period_stat].data.dbl /=
      router.opts().num_proxies;
  }
#ifndef FBCODE_OPT_BUILD
  stats[mc_msg_num_outstanding_stat].data.uint64 =
    mc_msg_num_outstanding();
#endif

  for (int i = 0; i < num_stats; i++) {
    if (stats[i].aggregate && !(stats[i].group & rate_stats)) {
      for (size_t j = 0; j < router.opts().num_proxies; ++j) {
        auto pr = router.getProxy(j);
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
}

void stat_incr(stat_t* stats, stat_name_t stat_num, int64_t amount) {
  stats[stat_num].data.uint64 += amount;
}

void stat_decr(stat_t* stats, stat_name_t stat_num, int64_t amount) {
  stat_incr(stats, stat_num, -amount);
}

// Thread-safe increment of the given counter
void stat_incr_safe(stat_t* stats, stat_name_t stat_name) {
  __sync_fetch_and_add(&stats[stat_name].data.uint64, 1);
}

void stat_decr_safe(stat_t* stats, stat_name_t stat_name) {
  __sync_fetch_and_add(&stats[stat_name].data.uint64, -1);
}

void stat_set_uint64(stat_t* stats,
                     stat_name_t stat_num,
                     uint64_t value) {
  stat_t* stat = &stats[stat_num];
  assert(stat->type == stat_uint64);
  stat->data.uint64 = value;
}

uint64_t stat_get_uint64(stat_t* stats, stat_name_t stat_num) {
  stat_t* stat = &stats[stat_num];
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
  } else if (str == "servers") {
    return server_stats;
  } else if (str == "suspect_servers") {
    return suspect_server_stats;
  } else if (str == "memory") {
    return memory_stats;
  } else if (str == "count") {
    return count_stats;
  } else if (str == "outlier") {
    return outlier_stats;
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

  prepare_stats(proxy->router(), stats);

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

  if (groups & server_stats) {
    folly::StringKeyedUnorderedMap<ServerStat> serverStats;
    auto& router = proxy->router();
    for (size_t i = 0; i < router.opts().num_proxies; ++i) {
      router.getProxy(i)->destinationMap->foreachDestinationSynced(
        [&serverStats](folly::StringPiece key, const ProxyDestination& pdstn) {
          auto& stat = serverStats[key];
          stat.isHardTko = pdstn.tracker->isHardTko();
          stat.isSoftTko = pdstn.tracker->isSoftTko();
          if (pdstn.stats().results) {
            for (size_t j = 0; j < mc_nres; ++j) {
              stat.results[j] += (*pdstn.stats().results)[j];
            }
          }
          ++stat.states[(size_t)pdstn.stats().state];

          if (pdstn.stats().avgLatency.hasValue()) {
            stat.sumLatencies += pdstn.stats().avgLatency.value();
            ++stat.cntLatencies;
          }
          stat.pendingRequestsCount += pdstn.getPendingRequestCount();
          stat.inflightRequestsCount += pdstn.getInflightRequestCount();
        }
      );
    }
    for (const auto& it : serverStats) {
      reply.addStat(it.first, it.second.toString());
    }
  }

  if (groups & suspect_server_stats) {
    auto suspectServers = proxy->router().tkoTrackerMap().getSuspectServers();
    for (const auto& it : suspectServers) {
      reply.addStat(it.first, folly::format("status:{} num_failures:{}",
                                            it.second.first ? "tko" : "down",
                                            it.second.second).str());
    }
  }

  return reply.getMcReply();
}

void set_standalone_args(folly::StringPiece args) {
  assert(gStandaloneArgs == nullptr);
  gStandaloneArgs = new char[args.size() + 1];
  ::memcpy(gStandaloneArgs, args.begin(), args.size());
  gStandaloneArgs[args.size()] = 0;
}

}}} // facebook::memcache::mcrouter
