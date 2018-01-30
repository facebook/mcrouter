/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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

#include <limits>

#include <folly/Conv.h>
#include <folly/Range.h>
#include <folly/json.h>

#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/StatsReply.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/network/gen/Memcache.h"

/**                             .__
 * __  _  _______ _______  ____ |__| ____    ____
 * \ \/ \/ /\__  \\_  __ \/    \|  |/    \  / ___\
 *  \     /  / __ \|  | \/   |  \  |   |  \/ /_/  >
 *   \/\_/  (____  /__|  |___|  /__|___|  /\___  /
 *               \/           \/        \//_____/
 *
 * Read the following code with proper care for life and limb.
 */

namespace facebook {
namespace memcache {
namespace mcrouter {

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
  double sumRetransPerKByte{0.0};
  size_t cntRetransPerKByte{0};
  double maxRetransPerKByte{0.0};
  double minRetransPerKByte{std::numeric_limits<double>::infinity()};

  std::string toString() const {
    double avgLatency = cntLatencies == 0 ? 0 : sumLatencies / cntLatencies;
    auto res = folly::format("avg_latency_us:{:.3f}", avgLatency).str();
    folly::format(" pending_reqs:{}", pendingRequestsCount).appendTo(res);
    folly::format(" inflight_reqs:{}", inflightRequestsCount).appendTo(res);
    if (isHardTko) {
      res.append(" hard_tko; ");
    } else if (isSoftTko) {
      res.append(" soft_tko; ");
    }
    if (cntRetransPerKByte > 0) {
      double avgRetransPerKByte = sumRetransPerKByte / cntRetransPerKByte;
      folly::format(
          " avg_retrans_ratio:{} max_retrans_ratio:{} min_retrans_ratio:{}",
          avgRetransPerKByte,
          maxRetransPerKByte,
          minRetransPerKByte)
          .appendTo(res);
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
        folly::format("{} {}:{}", firstResult ? ";" : "", result, results[i])
            .appendTo(res);
        firstResult = false;
      }
    }
    return res;
  }
};

int get_num_bins_used(const CarbonRouterInstanceBase& router) {
  if (router.opts().num_proxies > 0) {
    auto anyProxy = router.getProxyBase(0);
    if (anyProxy) {
      return anyProxy->stats().numBinsUsed();
    }
  }
  return 0;
}

double stats_rate_value(ProxyBase* proxy, int idx) {
  const stat_t& stat = proxy->stats().getStat(idx);
  double rate = 0;

  if (proxy->stats().numBinsUsed() != 0) {
    if (stat.aggregate) {
      rate = stats_aggregate_rate_value(proxy->router(), idx);
    } else {
      rate = (double)proxy->stats().getStatValueWithinWindow(idx) /
          (proxy->stats().numBinsUsed() * MOVING_AVERAGE_BIN_SIZE_IN_SECOND);
    }
  }

  return rate;
}

uint64_t stats_max_value(ProxyBase* proxy, int idx) {
  return stats_aggregate_max_value(proxy->router(), idx);
}

} // anonymous namespace

// This is a subset of what's in proc(5).
struct proc_stat_data_t {
  unsigned long num_minor_faults;
  unsigned long num_major_faults;
  double user_time_sec;
  double system_time_sec;
  unsigned long vsize;
  unsigned long rss;
};

double stats_aggregate_rate_value(
    const CarbonRouterInstanceBase& router,
    int idx) {
  double rate = 0;
  int num_bins_used = get_num_bins_used(router);

  if (num_bins_used != 0) {
    uint64_t num = 0;
    for (size_t i = 0; i < router.opts().num_proxies; ++i) {
      num += router.getProxyBase(i)->stats().getStatValueWithinWindow(idx);
    }
    rate = (double)num / (num_bins_used * MOVING_AVERAGE_BIN_SIZE_IN_SECOND);
  }

  return rate;
}

uint64_t stats_aggregate_max_value(
    const CarbonRouterInstanceBase& router,
    int idx) {
  uint64_t max = 0;
  int num_bins_used = get_num_bins_used(router);

  for (int j = 0; j < num_bins_used; ++j) {
    uint64_t binSum = 0;
    for (size_t i = 0; i < router.opts().num_proxies; ++i) {
      binSum += router.getProxyBase(i)->stats().getStatBinValue(idx, j);
    }
    max = std::max(max, binSum);
  }

  return max;
}

uint64_t stats_aggregate_max_max_value(
    const CarbonRouterInstanceBase& router,
    int idx) {
  uint64_t max = 0;
  int num_bins_used = get_num_bins_used(router);

  for (int j = 0; j < num_bins_used; ++j) {
    for (size_t i = 0; i < router.opts().num_proxies; ++i) {
      max = std::max(
          max, router.getProxyBase(i)->stats().getStatBinValue(idx, j));
    }
  }
  return max;
}

static std::string rate_stat_to_str(ProxyBase* proxy, int idx) {
  return folly::stringPrintf("%g", stats_rate_value(proxy, idx));
}

static std::string max_stat_to_str(ProxyBase* proxy, int idx) {
  return folly::to<std::string>(stats_max_value(proxy, idx));
}

static std::string max_max_stat_to_str(ProxyBase* proxy, int idx) {
  return folly::to<std::string>(
      stats_aggregate_max_max_value(proxy->router(), idx));
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
static std::string stat_to_str(const stat_t* stat, void* /* ptr */) {
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
      LOG(ERROR) << "unknown stat type " << stat->type << " (" << stat->name
                 << ")";
      return "";
  }
}

void init_stats(stat_t* stats) {
#define STAT(_name, _type, _aggregate, _data_assignment) \
  {                                                      \
    stat_t& s = stats[_name##_stat];                     \
    s.name = #_name;                                     \
    s.group = GROUP;                                     \
    s.type = _type;                                      \
    s.aggregate = _aggregate;                            \
    s.data _data_assignment;                             \
  }
#define STUI(name, value, agg) STAT(name, stat_uint64, agg, .uint64 = value)
#define STUIR(name, value, agg) STAT(name, stat_uint64, agg, .uint64 = value)
#define STSI(name, value, agg) STAT(name, stat_int64, agg, .int64 = value)
#define STSS(name, value, agg) \
  STAT(name, stat_string, agg, .string = (char*)value)
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
static int get_proc_stat(pid_t pid, proc_stat_data_t* data) {
  data->num_minor_faults = 0;
  data->num_major_faults = 0;
  data->user_time_sec = 0.0;
  data->system_time_sec = 0.0;
  data->vsize = 0;
  data->rss = 0;

  char stat_path[32];
  snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);

  FILE* stat_file = fopen(stat_path, "r");
  if (stat_file == nullptr) {
    LOG(ERROR) << "Can't open process status information file: " << stat_path
               << ": " << strerror(errno);
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

  int count = fscanf(
      stat_file,
      "%*d (%*[^)]) %*c %*d %*d %*d %*d %*d %*u %lu "
      "%*u %lu %*u %lu %lu %*d %*d %*d %*d %*d "
      "%*d %*u %lu %ld" /* and there's more */,
      &data->num_minor_faults,
      &data->num_major_faults,
      &utime_ticks,
      &stime_ticks,
      &data->vsize,
      &rss_pages);
  fclose(stat_file);

  if (count != 6) {
    return -1;
  }

  data->user_time_sec = ((double)utime_ticks) / sysconf(_SC_CLK_TCK);
  data->system_time_sec = ((double)stime_ticks) / sysconf(_SC_CLK_TCK);

  // rss is documented to be signed, but since negative numbers are
  // nonsensical, and nothing else is in pages, we clamp it and
  // convert to bytes here.

  data->rss =
      rss_pages < 0 ? 0ul : (unsigned long)(rss_pages * sysconf(_SC_PAGESIZE));

  return 0;
}

void prepare_stats(CarbonRouterInstanceBase& router, stat_t* stats) {
  init_stats(stats);

  uint64_t config_last_success = 0;
  uint64_t destinationBatchesSum = 0;
  uint64_t destinationRequestsSum = 0;
  uint64_t outstandingGetReqsTotal = 0;
  uint64_t outstandingGetReqsHelper = 0;
  uint64_t outstandingGetWaitTimeSumUs = 0;
  uint64_t outstandingUpdateReqsTotal = 0;
  uint64_t outstandingUpdateReqsHelper = 0;
  uint64_t outstandingUpdateWaitTimeSumUs = 0;
  uint64_t retransPerKByteSum = 0;
  uint64_t retransNumTotal = 0;

  for (size_t i = 0; i < router.opts().num_proxies; ++i) {
    auto proxy = router.getProxyBase(i);
    config_last_success = std::max(
        config_last_success, proxy->stats().getValue(config_last_success_stat));
    destinationBatchesSum +=
        proxy->stats().getStatValueWithinWindow(destination_batches_sum_stat);
    destinationRequestsSum +=
        proxy->stats().getStatValueWithinWindow(destination_requests_sum_stat);

    outstandingGetReqsTotal += proxy->stats().getStatValueWithinWindow(
        outstanding_route_get_reqs_queued_stat);
    outstandingGetReqsHelper += proxy->stats().getStatValueWithinWindow(
        outstanding_route_get_reqs_queued_helper_stat);
    outstandingGetWaitTimeSumUs += proxy->stats().getStatValueWithinWindow(
        outstanding_route_get_wait_time_sum_us_stat);
    outstandingUpdateReqsTotal += proxy->stats().getStatValueWithinWindow(
        outstanding_route_update_reqs_queued_stat);
    outstandingUpdateReqsHelper += proxy->stats().getStatValueWithinWindow(
        outstanding_route_update_reqs_queued_helper_stat);
    outstandingUpdateWaitTimeSumUs += proxy->stats().getStatValueWithinWindow(
        outstanding_route_update_wait_time_sum_us_stat);

    retransPerKByteSum +=
        proxy->stats().getStatValueWithinWindow(retrans_per_kbyte_sum_stat);
    retransNumTotal +=
        proxy->stats().getStatValueWithinWindow(retrans_num_total_stat);
  }

  stat_set_uint64(
      stats,
      num_suspect_servers_stat,
      router.tkoTrackerMap().getSuspectServersCount());

  double avgBatchSize = 0.0;
  if (destinationBatchesSum != 0) {
    avgBatchSize = destinationRequestsSum / (double)destinationBatchesSum;
  }
  stats[destination_batch_size_stat].data.dbl = avgBatchSize;

  double avgRetransPerKByte = 0.0;
  if (retransNumTotal != 0) {
    avgRetransPerKByte = retransPerKByteSum / (double)retransNumTotal;
  }
  stats[retrans_per_kbyte_avg_stat].data.dbl = avgRetransPerKByte;

  stats[outstanding_route_get_avg_queue_size_stat].data.dbl = 0.0;
  stats[outstanding_route_get_avg_wait_time_sec_stat].data.dbl = 0.0;
  if (outstandingGetReqsTotal > 0) {
    stats[outstanding_route_get_avg_queue_size_stat].data.dbl =
        outstandingGetReqsHelper / (double)outstandingGetReqsTotal;
    stats[outstanding_route_get_avg_wait_time_sec_stat].data.dbl =
        outstandingGetWaitTimeSumUs / (1000000.0 * outstandingGetReqsTotal);
  }

  stats[outstanding_route_update_avg_queue_size_stat].data.dbl = 0.0;
  stats[outstanding_route_update_avg_wait_time_sec_stat].data.dbl = 0.0;
  if (outstandingUpdateReqsTotal > 0) {
    stats[outstanding_route_update_avg_queue_size_stat].data.dbl =
        outstandingUpdateReqsHelper / (double)outstandingUpdateReqsTotal;
    stats[outstanding_route_update_avg_wait_time_sec_stat].data.dbl =
        outstandingUpdateWaitTimeSumUs /
        (1000000.0 * outstandingUpdateReqsTotal);
  }

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
  stats[configs_from_disk_stat].data.uint64 =
      static_cast<uint64_t>(router.configuredFromDisk());

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
    auto pr = router.getProxyBase(i);
    stats[fibers_allocated_stat].data.uint64 +=
        pr->fiberManager().fibersAllocated();
    stats[fibers_pool_size_stat].data.uint64 +=
        pr->fiberManager().fibersPoolSize();
    stats[fibers_stack_high_watermark_stat].data.uint64 = std::max(
        stats[fibers_stack_high_watermark_stat].data.uint64,
        pr->fiberManager().stackHighWatermark());
    stats[duration_us_stat].data.dbl += pr->stats().durationUs().value();
    stats[client_queue_notify_period_stat].data.dbl += pr->queueNotifyPeriod();
  }

  if (router.opts().num_proxies > 0) {
    stats[duration_us_stat].data.dbl /= router.opts().num_proxies;
    stats[client_queue_notify_period_stat].data.dbl /=
        router.opts().num_proxies;
  }

  for (int i = 0; i < num_stats; i++) {
    if (stats[i].aggregate && !(stats[i].group & rate_stats)) {
      for (size_t j = 0; j < router.opts().num_proxies; ++j) {
        auto pr = router.getProxyBase(j);
        if (stats[i].type == stat_uint64) {
          stats[i].data.uint64 += pr->stats().getStat(i).data.uint64;
        } else if (stats[i].type == stat_int64) {
          stats[i].data.int64 += pr->stats().getStat(i).data.int64;
        } else if (stats[i].type == stat_double) {
          stats[i].data.dbl += pr->stats().getStat(i).data.dbl;
        } else {
          LOG(FATAL) << "you can't aggregate non-numerical stats!";
        }
      }
    }
  }
}

// Thread-safe increment of the given counter
void stat_incr_safe(stat_t* stats, stat_name_t stat_name, int64_t amount) {
  __sync_fetch_and_add(&stats[stat_name].data.uint64, amount);
}

void stat_decr_safe(stat_t* stats, stat_name_t stat_name) {
  __sync_fetch_and_add(&stats[stat_name].data.uint64, -1);
}

void stat_set_uint64(stat_t* stats, stat_name_t stat_num, uint64_t value) {
  stat_t* stat = &stats[stat_num];
  assert(stat->type == stat_uint64);
  stat->data.uint64 = value;
}

uint64_t stat_get_uint64(const stat_t* stats, stat_name_t stat_num) {
  return stats[stat_num].data.uint64;
}

static stat_group_t stat_parse_group_str(folly::StringPiece str) {
  if (str == "all") {
    return all_stats;
  } else if (str == "detailed") {
    return detailed_stats;
  } else if (str == "cmd-error") {
    return cmd_error_stats;
  } else if (str == "ods") {
    return ods_stats;
  } else if (str == "servers") {
    return server_stats;
  } else if (str == "suspect_servers") {
    return suspect_server_stats;
  } else if (str == "count") {
    return count_stats;
  } else if (str.empty()) {
    return mcproxy_stats;
  } else {
    return unknown_stats;
  }
}

/**
 * @param Proxy proxy
 */
McStatsReply stats_reply(ProxyBase* proxy, folly::StringPiece group_str) {
  auto lockGuard = proxy->stats().lock();

  StatsReply reply;

  if (group_str == "version") {
    reply.addStat("mcrouter-version", MCROUTER_PACKAGE_STRING);
    return reply.getReply();
  }

  auto groups = stat_parse_group_str(group_str);
  if (groups == unknown_stats) {
    McStatsReply errorReply(mc_res_client_error);
    errorReply.message() = "bad stats command";
    return errorReply;
  }

  stat_t stats[num_stats];

  prepare_stats(proxy->router(), stats);

  for (unsigned int ii = 0; ii < num_stats; ii++) {
    stat_t* stat = &stats[ii];
    if (stat->group & groups) {
      if (stat->group & rate_stats) {
        reply.addStat(stat->name, rate_stat_to_str(proxy, ii));
      } else if (stat->group & max_stats) {
        reply.addStat(stat->name, max_stat_to_str(proxy, ii));
      } else if (stat->group & max_max_stats) {
        reply.addStat(stat->name, max_max_stat_to_str(proxy, ii));
      } else {
        reply.addStat(stat->name, stat_to_str(stat, nullptr));
      }
    }
  }

  if (groups & (mcproxy_stats | all_stats | detailed_stats | ods_stats)) {
    folly::dynamic requestStats(folly::dynamic::object());
    const auto& router = proxy->router();
    for (size_t i = 0; i < router.opts().num_proxies; ++i) {
      const auto proxyRequestStats =
          router.getProxyBase(i)->dumpRequestStats(false /* filterZeroes */);
      for (const auto& k : proxyRequestStats.keys()) {
        requestStats.setDefault(k, 0) += proxyRequestStats[k];
      }
    }

    for (const auto& k : requestStats.keys()) {
      if (requestStats[k].isInt()) {
        reply.addStat(
            k.asString(), folly::to<std::string>(requestStats[k].asInt()));
      } else if (requestStats[k].isDouble()) {
        reply.addStat(
            k.asString(), folly::to<std::string>(requestStats[k].asDouble()));
      } else {
        MC_LOG_FAILURE(
            proxy->router().opts(),
            failure::Category::kOther,
            folly::sformat("Couldn't serialize Carbon stat {}", k.asString()));
      }
    }
  }

  if (groups & server_stats) {
    folly::StringKeyedUnorderedMap<ServerStat> serverStats;
    auto& router = proxy->router();
    for (size_t i = 0; i < router.opts().num_proxies; ++i) {
      router.getProxyBase(i)->destinationMap()->foreachDestinationSynced(
          [&serverStats](
              folly::StringPiece key, const ProxyDestination& pdstn) {
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

            if (pdstn.stats().retransPerKByte >= 0.0) {
              const auto val = pdstn.stats().retransPerKByte;
              stat.sumRetransPerKByte += val;
              stat.maxRetransPerKByte = std::max(stat.maxRetransPerKByte, val);
              stat.minRetransPerKByte = std::min(stat.minRetransPerKByte, val);
              ++stat.cntRetransPerKByte;
            }
            stat.pendingRequestsCount += pdstn.getPendingRequestCount();
            stat.inflightRequestsCount += pdstn.getInflightRequestCount();
          });
    }
    for (const auto& it : serverStats) {
      reply.addStat(it.first, it.second.toString());
    }
  }

  if (groups & suspect_server_stats) {
    auto suspectServers = proxy->router().tkoTrackerMap().getSuspectServers();
    for (const auto& it : suspectServers) {
      reply.addStat(
          it.first,
          folly::format(
              "status:{} num_failures:{}",
              it.second.first ? "tko" : "down",
              it.second.second)
              .str());
    }
  }

  return reply.getReply();
}

void set_standalone_args(folly::StringPiece args) {
  assert(gStandaloneArgs == nullptr);
  gStandaloneArgs = new char[args.size() + 1];
  ::memcpy(gStandaloneArgs, args.begin(), args.size());
  gStandaloneArgs[args.size()] = 0;
}
}
}
} // facebook::memcache::mcrouter
