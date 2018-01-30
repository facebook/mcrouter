/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyStats.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

ProxyStats::ProxyStats() {
  init_stats(stats_);
}

void ProxyStats::aggregate(size_t statId) {
  constexpr size_t kBinNum =
      MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND / MOVING_AVERAGE_BIN_SIZE_IN_SECOND;

  if (numBinsUsed_ < kBinNum) {
    ++numBinsUsed_;
  }

  for (int j = 0; j < num_stats; ++j) {
    if (stats_[j].group & rate_stats) {
      statsNumWithinWindow_[j] -= statsBin_[j][statId];
      statsBin_[j][statId] = stats_[j].data.uint64;
      statsNumWithinWindow_[j] += statsBin_[j][statId];
      stats_[j].data.uint64 = 0;
    } else if (stats_[j].group & (max_stats | max_max_stats)) {
      statsBin_[j][statId] = stats_[j].data.uint64;
      stats_[j].data.uint64 = 0;
    }
  }
}

std::unique_lock<std::mutex> ProxyStats::lock() const {
  return std::unique_lock<std::mutex>(mutex_);
}
}
}
} // facebook::memcache::mcrouter
