/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Accumulator.h"

#include <vector>

#include <glog/logging.h>

#include <folly/io/async/EventBase.h>
#include <folly/MPMCQueue.h>
#include <folly/Random.h>
#include <folly/ThreadLocal.h>

namespace facebook { namespace memcache { namespace cycles { namespace detail {

// Interval between data aggregations in milliseconds (must be strictly less
// than kExtractingIntervalMs).
constexpr size_t kAggregationIntervalMs = 1000;

namespace {

// Accumulated stats queue.
folly::MPMCQueue<CycleStats> gStatsQueue{100};

} // anonymous namespace

void Accumulator::add(const Interval& interval) {
  auto reqId = interval.label().requestId();
  auto idx = reqId % kMaxInflightSamples;

  if (inflightSamples_[idx].label == interval.label()) {
    // This interval is a part of an existing sample.
    inflightSamples_[idx].length += interval.length();
    inflightSamples_[idx].numIntervals++;
  } else {
    if (inflightSamples_[idx].numIntervals != 0 &&
        inflightSamples_[idx].contextSwitches == 0) {
      // Save previous sample
      if (numSamples_ < kMaxSamples) {
        samples_[numSamples_] = std::move(inflightSamples_[idx]);
      } else {
        // reservoir sampling.
        auto r = folly::Random::rand32(numSamples_ + 1);
        if (r < kMaxSamples) {
          samples_[r] = std::move(inflightSamples_[idx]);
        }
      }
      ++numSamples_;
    }

    inflightSamples_[idx].label = interval.label();
    inflightSamples_[idx].length = interval.length();
    inflightSamples_[idx].numIntervals = 1;
  }
}

void Accumulator::attachEventBase(folly::EventBase& eventBase) {
  if (eventBase_ != nullptr) {
    LOG(ERROR) << "Attempt to reattach EventBase to cycles Accumulator";
    return;
  }

  eventBase_ = &eventBase;

  scheduleNextAggregation();
}

CycleStats Accumulator::aggregate() {
  CycleStats stats;

  if (numSamples_ == 0) {
    return stats;
  }

  // Compute valid intervals and discard invalid.
  int lo = 0;
  int hi = kMaxSamples - 1;
  while (lo <= hi) {
    // Verify if sample is valid. We are using a heuristic where a valid
    // sample is composed by 2 intervals. Will want to change it in next version
    if (samples_[lo].numIntervals >= 2) {
      stats.avg += samples_[lo].length;
      ++lo;
    } else {
      std::swap(samples_[lo], samples_[hi]);
      --hi;
    }
  }
  stats.numSamples = lo;

  // Calculate percentils
  if (stats.numSamples > 0) {
    std::sort(samples_.begin(), samples_.begin() + stats.numSamples,
              [](const Sample& lhr, const Sample& rhs) {
                return lhr.length < rhs.length;
              });
    stats.min = samples_[0].length;
    stats.p01 = samples_[stats.numSamples * 0.01].length;
    stats.p05 = samples_[stats.numSamples * 0.05].length;
    stats.p50 = samples_[stats.numSamples * 0.50].length;
    stats.p95 = samples_[stats.numSamples * 0.95].length;
    stats.p99 = samples_[stats.numSamples * 0.99].length;
    stats.max = samples_[stats.numSamples - 1].length;

    stats.avg /= stats.numSamples;
  }

  // Reset and schedule next one.
  numSamples_ = 0;

  return stats;
}

void Accumulator::scheduleNextAggregation() {
  this->eventBase_->runAfterDelay([&] {
    CycleStats stats = this->aggregate();
    if (stats.numSamples > 0) {
      gStatsQueue.write(stats);
    }
    this->scheduleNextAggregation();
  }, kAggregationIntervalMs);
}

Accumulator& currentAccumulator() {
  static folly::ThreadLocal<Accumulator> accumulator_;
  return *accumulator_;
}

CycleStats extract() {
  CycleStats stats;
  stats.min = std::numeric_limits<uint64_t>::max();

  CycleStats tmp;
  while (gStatsQueue.read(tmp)) {
    stats.min = std::min(stats.min, tmp.min);
    stats.max = std::max(stats.max, tmp.max);
    stats.avg += (tmp.avg * tmp.numSamples);
    stats.p01 += (tmp.p01 * tmp.numSamples);
    stats.p05 += (tmp.p05 * tmp.numSamples);
    stats.p50 += (tmp.p50 * tmp.numSamples);
    stats.p95 += (tmp.p95 * tmp.numSamples);
    stats.p99 += (tmp.p99 * tmp.numSamples);
    stats.numSamples += tmp.numSamples;
  }

  if (stats.numSamples > 0) {
    stats.avg /= stats.numSamples;
    stats.p01 /= stats.numSamples;
    stats.p05 /= stats.numSamples;
    stats.p50 /= stats.numSamples;
    stats.p95 /= stats.numSamples;
    stats.p99 /= stats.numSamples;
  } else {
    stats.min = 0;
  }

  return stats;
}

}}}} // namespace facebook::memcache::cycles::detail
