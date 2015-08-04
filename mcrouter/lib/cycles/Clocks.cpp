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
#include <stdexcept>
#include <sys/time.h>
#include <sys/resource.h>

namespace facebook { namespace memcache { namespace cycles {

namespace {

inline uint64_t rdtsc() {
#if defined(__x86_64__)
  uint64_t hi, lo;
  __asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi));
  return (hi << 32) | lo;
#elif defined(__powerpc__) && \
    ( __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8))
  uint64_t val;
  val = __builtin_ppc_get_timebase();
  return val;
#else
#error Unsupported CPU. Consider implementing your own Clock.
#endif
}

} // anonymous namespace

Metering CyclesClock::read() const {
  return Metering{rdtsc(), 0};
}

Metering RUsageClock::read() const {
  rusage res;
#ifdef RUSAGE_THREAD
  getrusage(RUSAGE_THREAD, &res);
  return Metering{rdtsc(), (uint64_t)res.ru_nvcsw + res.ru_nivcsw};
#else
  throw std::runtime_error("RUSAGE_THREAD is not defined on this system.");
#endif
}

}}} // namespace facebook::memcache::cycles
