/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_FB_CPU_UTIL
#define FBI_FB_CPU_UTIL

#include <stdint.h>

__BEGIN_DECLS

extern double get_cpu_frequency(void);
extern void   bind_thread_to_cpu(int);

#ifdef __aarch64__
static inline double timer_frequency(void)
{
  uint64_t val;
  asm volatile ("mrs %[rt], cntfrq_el0" : [rt] "=r" (val)) ;
  return val;
}
#endif

static inline uint64_t cycle_timer(void) {
  uint64_t val;
#ifdef __aarch64__
  asm volatile ("mrs %[rt],cntvct_el0" : [rt] "=r" (val));
#elif defined(__powerpc__) && \
    ( __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8))
  val = __builtin_ppc_get_timebase();
#else
  uint32_t __a,__d;

  //cpuid();
  asm volatile("rdtsc" : "=a" (__a), "=d" (__d));
  (val) = ((uint64_t)__a) | (((uint64_t)__d)<<32);
#endif
  return val;
}

static inline double get_microseconds(double cpu_freq) {
#ifdef __aarch64__
  return cycle_timer() / timer_frequency();
#else
  return cycle_timer() / cpu_freq;
#endif
}


static inline uint64_t get_microsecond_from_tsc(uint64_t count,
                                                double cpu_frequency) {
#ifdef __aarch64__
  return count /  timer_frequency();
#else
  return count / cpu_frequency;
#endif
}

__END_DECLS

#endif
