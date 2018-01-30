/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <mutex>

#include "mcrouter/ExponentialSmoothData.h"
#include "mcrouter/stats.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

class ProxyStats {
 public:
  ProxyStats();

  /**
   * Aggregate proxy stat with the given index.
   * Caller must be holding stats lock (i.e. must call lock() before).
   *
   * @param statId   Index of the stat to aggregate.
   */
  void aggregate(size_t statId);

  /**
   * Lock stats for the duration of the lock_guard life.
   */
  std::unique_lock<std::mutex> lock() const;

  ExponentialSmoothData<64>& durationUs() {
    return durationUs_;
  }

  size_t numBinsUsed() const {
    return numBinsUsed_;
  }

  /**
   * Return value of stat "statId" in the "timeBinIdx" time bin.
   *
   * @param statId      Index of the stat.
   * @param timeBinIdx  Idx of the time bin.
   */
  uint64_t getStatBinValue(size_t statId, size_t timeBinIdx) const {
    return statsBin_[statId][timeBinIdx];
  }

  /**
   * Return the aggregated value of stat "statId" in the past
   * MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND seconds.
   * NOTE: This is only computed for rate_stats.
   *
   * @param statId      Index of the stat.
   */
  uint64_t getStatValueWithinWindow(size_t statId) const {
    return statsNumWithinWindow_[statId];
  }

  /**
   * Get the rate value of the stat "statId".
   *
   * @param statId      Index of the stat.
   */
  double getRateValue(size_t statId) const;

  /**
   * Increment the stat.
   *
   * @param stat    Stat to increment
   * @param amount  Amount to increment the stat
   */
  void increment(stat_name_t stat, int64_t amount = 1) {
    stat_incr(stats_, stat, amount);
  }

  /**
   * Decrement the stat.
   *
   * @param stat    Stat to decrement
   * @param amount  Amount to decrement the stat
   */
  void decrement(stat_name_t stat, int64_t amount = 1) {
    increment(stat, -amount);
  }

  /**
   * Increment the stat. Thread-safe.
   *
   * @param stat    Stat to increment
   * @param amount  Amount to increment the stat
   */
  void incrementSafe(stat_name_t stat, int64_t amount = 1) {
    stat_incr_safe(stats_, stat, amount);
  }

  /**
   * Decrement the stat. Thread-safe.
   *
   * @param stat    Stat to decrement
   * @param amount  Amount to decrement the stat
   */
  void decrementSafe(stat_name_t stat, int64_t amount = 1) {
    incrementSafe(stat, -amount);
  }

  /**
   * Set the stat value.
   *
   * @param stat      Stat to set
   * @param newValue  New value of the stat
   */
  void setValue(stat_name_t stat, int64_t newValue) {
    stat_set_uint64(stats_, stat, newValue);
  }

  uint64_t getValue(stat_name_t stat) const {
    return stat_get_uint64(stats_, stat);
  }
  uint64_t getConfigAge(uint64_t now) const {
    return stat_get_config_age(stats_, now);
  }
  const stat_t& getStat(size_t statId) const {
    return stats_[statId];
  }

 private:
  mutable std::mutex mutex_;
  stat_t stats_[num_stats]{};

  ExponentialSmoothData<64> durationUs_;

  // we are wasting some memory here to get faster mapping from stat name to
  // statsBin_[] and statsNumWithinWindow_[] entry. i.e., the statsBin_[]
  // and statsNumWithinWindow_[] entry for non-rate stat are not in use.

  // we maintain some information for calculating average rate in the past
  // MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND seconds for every rate stat.

  /*
   * statsBin_[stat_name] is a circular array associated with stat "stat_name",
   * where each element (statsBin_[stat_name][idx]) is either the count (if it
   * is a rate_stat) or the max (if it is a max_stat) of "stat_name" in the
   * "idx"th time bin. The updater thread updates these circular arrays once
   * every MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND second by setting the oldest
   * time bin to stats[stat_name], and then reset stats[stat_name] to 0.
   */
  uint64_t statsBin_[num_stats]
                    [MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND /
                     MOVING_AVERAGE_BIN_SIZE_IN_SECOND]{};
  /*
   * statsNumWithinWindow_[stat_name] contains the count of stat "stat_name"
   * in the past MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND seconds. This array is
   * also updated by the updater thread once every
   * MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND seconds
   */
  uint64_t statsNumWithinWindow_[num_stats]{};

  /*
   * the number of bins currently used, which is initially set to 0, and is
   * increased by 1 every MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND seconds.
   * numBinsUsed_ is at most MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND /
   * MOVING_AVERAGE_BIN_SIZE_IN_SECOND
   */
  size_t numBinsUsed_{0};
};
}
}
} // facebook::memcache::mcrouter
