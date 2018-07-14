/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <folly/experimental/StringKeyedUnorderedMap.h>

#include "mcrouter/ExponentialSmoothData.h"
#include "mcrouter/stats.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

class PoolStats {
 public:
  PoolStats(folly::StringPiece poolName)
      : requestsCountStatName_(
            folly::to<std::string>(poolName, ".requests.sum")),
        finalResultErrorStatName_(
            folly::to<std::string>(poolName, ".final_result_error.sum")),
        durationUsStatName_(
            folly::to<std::string>(poolName, ".duration_us.avg")),
        totalDurationUsStatName_(
            folly::to<std::string>(poolName, ".total_duration_us.avg")) {
    initStat(requestCountStat_, requestsCountStatName_);
    initStat(finalResultErrorStat_, finalResultErrorStatName_);
  }

  std::vector<stat_t> getStats() const {
    stat_t durationStat;
    stat_t totalDurationStat;
    initStat(durationStat, durationUsStatName_);
    initStat(totalDurationStat, totalDurationUsStatName_);
    durationStat.data.uint64 = durationUsStat_.value();
    totalDurationStat.data.uint64 = totalDurationUsStat_.value();

    return {requestCountStat_,
            finalResultErrorStat_,
            std::move(durationStat),
            std::move(totalDurationStat)};
  }

  void incrementRequestCount(uint64_t amount = 1) {
    requestCountStat_.data.uint64 += amount;
  }

  void incrementFinalResultErrorCount(uint64_t amount = 1) {
    finalResultErrorStat_.data.uint64 += amount;
  }

  void addDurationSample(int64_t duration) {
    durationUsStat_.insertSample(duration);
  }

  void addTotalDurationSample(int64_t duration) {
    totalDurationUsStat_.insertSample(duration);
  }

 private:
  void initStat(stat_t& stat, folly::StringPiece name) const {
    stat.name = name;
    stat.group = ods_stats | count_stats;
    stat.type = stat_uint64;
    stat.aggregate = 0;
    stat.data.uint64 = 0;
  }

  const std::string requestsCountStatName_;
  const std::string finalResultErrorStatName_;
  const std::string durationUsStatName_;
  const std::string totalDurationUsStatName_;
  stat_t requestCountStat_;
  stat_t finalResultErrorStat_;
  ExponentialSmoothData<64> totalDurationUsStat_;
  ExponentialSmoothData<64> durationUsStat_;
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
