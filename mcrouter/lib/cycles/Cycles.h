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

#include <cstdint>
#include <memory>
#include <string>
#include <functional>

#include "mcrouter/lib/cycles/Clocks.h"
#include "mcrouter/lib/cycles/Interval.h"

namespace folly {
class EventBase;
}

/**
 * folly::cycles is a high-performance API for measuring CPU cycles
 * of applications that do asynchronous request processing.
 */
namespace facebook { namespace memcache { namespace cycles {

/**
 * Setup function that must be called once for each EventBase that is used to
 * process requests monitored by this API.
 *
 * Note: Must be called in EventBase's thread.
 */
void attachEventBase(folly::EventBase& eventBase);

/**
 * This class represents a time interval. A stopwatch will start at
 * constructor and stop during destruction.
 * Nested intervals are not supported.
 *
 * Note: For an interval to be valid, it has to have a label (i.e. it is
 * necessary to call cycles::label() between construction and destruction of
 * IntervaGuard).
 */
class IntervalGuard {
 public:
  /**
   * Starts a new interval section.
   */
  IntervalGuard();

  /**
   * Ends this interval section.
   */
  ~IntervalGuard();

 private:
  // Whether this interval is valid.
  bool valid_{false};
};

/**
 * Starts a new interval.
 * Note: nested intervals are not supportted.
 *
 * return   False case there is already one interval running
 *          (i.e. start() was called but not finish()). True otherwise.
 */
bool start() noexcept;

/**
 * Finishs an interval.
 */
void finish() noexcept;

/**
 * Gives a key to current interval. Intervals with the same key will be
 * aggregated together.
 *
 * @param   requestType Type (category) of this request.
 * @param   requestId   Id of this request. The request id must be unique per
 *                      thread processing requests.
 * @returns             True if the interval was correctly labeled. False
 *                      otherwise (e.g. there is no active interval, the current
 *                      interval is already labeled, etc).
 */
bool label(uint64_t requestType, uint64_t requestId);

/**
 * Holds statistical information about CPU cycles usage of requests.
 */
struct CycleStats {
  // Min/max
  uint64_t min{0};
  uint64_t max{0};

  // Average
  uint64_t avg{0};

  // Percentiles
  uint64_t p01{0};
  uint64_t p05{0};
  uint64_t p50{0};
  uint64_t p95{0};
  uint64_t p99{0};

  // Number of samples
  size_t numSamples{0};
};

/**
 * Starts extracting data.
 * This function does nothing if extraction is already running.
 *
 * Note: Before start extracting, it is necessary to have attached all relevant
 * EventBases through attachEventBase().
 *
 * @param func  Function that will receive the extracted data.
 */
void startExtracting(std::function<void(CycleStats)> func);

/**
 * Stops extractor thread.
 */
void stopExtracting();

}}} // namespace facebook::memcache::cycles
