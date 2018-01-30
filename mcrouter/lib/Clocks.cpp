/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Clocks.h"

namespace facebook {
namespace memcache {
namespace cycles {

uint64_t getCpuCycles() noexcept {
#if defined(__x86_64__)
  uint64_t hi;
  uint64_t lo;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return (hi << 32) | lo;
#elif defined(__powerpc__) && \
    (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8))
  uint64_t val;
  val = __builtin_ppc_get_timebase();
  return val;
#elif defined(__aarch64__)
  uint64_t cval;
  asm volatile("mrs %0, cntvct_el0" : "=r"(cval));
  return cval;
#else
#error Unsupported CPU. Consider implementing your own Clock.
#endif
}

} // cycles
} // memcache
} // facebook
