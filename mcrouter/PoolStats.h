/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <folly/experimental/StringKeyedUnorderedMap.h>

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
            folly::to<std::string>(poolName, ".final_result_error.sum")) {
    initStat(requestCountStat_, requestsCountStatName_);
    initStat(finalResultErrorStat_, finalResultErrorStatName_);
  }

  std::vector<stat_t> getStats() const {
    return {requestCountStat_, finalResultErrorStat_};
  }

  void incrementRequestCount(uint64_t amount = 1) {
    requestCountStat_.data.uint64 += amount;
  }

  void incrementFinalResultErrorCount(uint64_t amount = 1) {
    finalResultErrorStat_.data.uint64 += amount;
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
  stat_t requestCountStat_;
  stat_t finalResultErrorStat_;
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
