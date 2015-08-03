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

namespace facebook { namespace memcache { namespace cycles {

struct Metering {
  // Number of ticks
  uint64_t ticks;
  // Number of context switches
  uint64_t contextSwitches;
};
inline Metering operator-(const Metering& lhs, const Metering& rhs) {
  return Metering{
    lhs.ticks - rhs.ticks,
    lhs.contextSwitches - rhs.contextSwitches
  };
}

/**
 * Abstract base class for clocks.
 */
class Clock {
 public:
  virtual ~Clock() noexcept {}

  /**
   * Reads the clock.
   */
  virtual Metering read() const = 0;
};

/**
 * Returns the number of cpu cycles since power-on. This clock doesn't involve
 * a system call. This clock does not measure context switches.
 * Thread-safe.
 * NOTE: Not all cpu and operating systems guarantee that this clock is
 * synchronized and increments constantly across all cpu cores.
 * This clock has no serializing instruction, which means that for some cpu
 * implementations this clock might be inaccurate for measuring a really small
 * amount of instructions due to out-of-order execution.
 */
class CyclesClock : public Clock {
 public:
  Metering read() const override;
};

/**
 * This clock adds context switches measurement capability to CyclesClock.
 */
class RUsageClock : public Clock {
 public:
  Metering read() const override;
};

}}} // namespace facebook::memcache::cycles
