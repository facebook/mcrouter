/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Clocks.h"

#include <chrono>

namespace facebook { namespace memcache { namespace cycles {

namespace {

inline uint64_t rdtsc() {
#if defined(__x86_64__)
  uint64_t hi, lo;
  __asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi));
  return (hi << 32) | lo;
#else
#error Unsupported CPU. Consider implementing your own Clock.
#endif
}

} // anonymous namespace

uint64_t CyclesClock::ticks() const {
  return rdtsc();
}

}}} // namespace facebook::memcache::cycles
