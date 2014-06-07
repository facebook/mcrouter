/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#ifndef FBI_FB_CPU_UTIL
#define FBI_FB_CPU_UTIL

__BEGIN_DECLS

extern double get_cpu_frequency(void);
extern void   bind_thread_to_cpu(int);

static inline uint64_t cycle_timer(void) {
  uint32_t __a,__d;
  uint64_t val;

  //cpuid();
  asm volatile("rdtsc" : "=a" (__a), "=d" (__d));
  (val) = ((uint64_t)__a) | (((uint64_t)__d)<<32);
  return val;
}

static inline double get_microseconds(double cpu_freq) {
  return cycle_timer() / cpu_freq;
}


static inline uint64_t get_microsecond_from_tsc(uint64_t count,
                                                double cpu_frequency) {
  return count / cpu_frequency;
}

__END_DECLS

#endif
