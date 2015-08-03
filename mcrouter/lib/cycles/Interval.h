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

#include <limits>
#include <string>

#include "mcrouter/lib/cycles/Clocks.h"

namespace facebook { namespace memcache { namespace cycles {

// Forward declarations
class IntervalGuard;

namespace detail {

/**
 * Label of the interval. Intervals with the same label will be aggregated
 * together.
 */
struct IntervalLabel {
 public:
  IntervalLabel() = default;
  IntervalLabel(uint64_t reqType, uint64_t reqId)
    : requestType_(reqType)
    , requestId_(reqId) {}

  uint16_t requestType() const {
    return requestType_;
  }

  uint64_t requestId() const {
    return requestId_;
  }

  friend bool operator==(const IntervalLabel& lhs, const IntervalLabel& rhs);
 private:
  uint64_t requestType_;
  uint64_t requestId_;
};

inline bool operator==(const IntervalLabel& lhs, const IntervalLabel& rhs) {
  return
    (lhs.requestType_ == rhs.requestType_) &&
    (lhs.requestId_   == rhs.requestId_);
}

/**
 * Represents a time interval.
 */
class Interval {
 public:
  /**
   * Constructor an invalid interval.
   */
  Interval() = default;

  /**
   * Constructs a labeled interval.
   *
   * @param metering      Holds the duration and the number of context switches
   *                      of the interval.
   * @param lbl           Label of this interval.
   */
  Interval(Metering metering, IntervalLabel lbl)
    : metering_(metering)
    , label_(std::move(lbl)) {}

  /**
   * Returns the duration of this interval
   */
  uint64_t length() const{
    return metering_.ticks;
  }

  /**
   * Returns the number of context switches that happened during this interval.
   */
  uint64_t contextSwitches() const{
    return metering_.contextSwitches;
  }

  /**
   * Label of this interval.
   */
  const IntervalLabel& label() const {
    return label_;
  }

 private:
  // Holds the length (duration) and the number of context swtiches
  // of this interval.
  Metering metering_{0, 0};

  // Label (key) of this interval.
  IntervalLabel label_;
};

}}}} // namespace facebook::memcache::cycles::detail
