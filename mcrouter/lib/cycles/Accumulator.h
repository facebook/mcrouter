/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <array>

#include "mcrouter/lib/cycles/Cycles.h"
#include "mcrouter/lib/cycles/Interval.h"

namespace folly {
class EventBase;
}

namespace facebook { namespace memcache { namespace cycles { namespace detail {

/**
 * Represents a sample - formed by one or more Intervals related to the
 * same label/request.
 */
struct Sample {
  IntervalLabel label{0, 0};
  uint64_t length{0};
  uint64_t contextSwitches{0};
  size_t numIntervals{0};
};

/**
 * Class responsible for aggregating together and computing statical
 * information about intervals.
 *
 * Note: This class is not thread-safe. All interactions with it should use the
 * same thread.
 */
class Accumulator {
 public:
  /**
   * Accumulates an interval.
   */
  void add(const Interval& interval);

  /**
   * Attachs an event base to this accumulator.
   */
  void attachEventBase(folly::EventBase& eventBase);

 private:
  // Number of in-flight samples to keep.
  static constexpr size_t kMaxInflightSamples = 3000;

  // Number of samples to keep.
  static constexpr size_t kMaxSamples = 3000;

  // Informs whether the event base was attached.
  folly::EventBase* eventBase_{nullptr};

  // Samples
  std::array<Sample, kMaxInflightSamples> inflightSamples_;
  std::array<Sample, kMaxSamples> samples_;
  size_t numSamples_{0};

  // Schedules next data aggregation for this accumulator.
  void scheduleNextAggregation();

  // Aggregates and extracts accumulated information from this accumulator.
  CycleStats aggregate();

  friend CycleStats extract();
};

/**
 * Returns the accumulator associated with current thread.
 */
Accumulator& currentAccumulator();

/**
 * Extracts accumulated information from all Accumulators (there is one
 * accumulator per thread).
 */
CycleStats extract();

}}}} // namespace facebook::memcache::cycles::detail
