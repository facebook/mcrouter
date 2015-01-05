/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <stdio.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef __FreeBSD__
#include <sys/resource.h>
#include <sys/cpuset.h>
#else
 #ifndef __USE_GNU
  #define __USE_GNU
 #endif
#include <sched.h>
#endif
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <err.h>
#include "fb_cpu_util.h"

#define MEGAHERTZ 1000000

static long get_usec_interval(struct timeval *start, struct timeval *end) {
    return (((end->tv_sec - start->tv_sec) * MEGAHERTZ)
            + (end->tv_usec - start->tv_usec));
}


double get_cpu_frequency(void) {
    struct timeval start;
    struct timeval end;
    uint64_t tsc_start;
    uint64_t tsc_end;
    long usec;

    if (gettimeofday(&start, 0)) {
        err(1, "gettimeofday");
    }

    tsc_start = cycle_timer();
    usleep(10000);

    if (gettimeofday(&end, 0)) {
        err(1, "gettimeofday");
    }
    tsc_end = cycle_timer();
    usec = get_usec_interval(&start, &end);
    return (tsc_end - tsc_start) * 1.0 / usec;
}


#if defined(__FreeBSD__)
void bind_thread_to_cpu(int cpuid) {
    cpuset_t mask;

    memset(&mask, 0, sizeof(mask));

    // bind this thread to a single cpu
    CPU_SET(cpuid, &mask);
    if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1,
                           sizeof(mask), &mask) < 0) {
      errx(1, "cpuset_setaffinity");
      return;
    }
}
#elif defined(__APPLE_CC__)
void bind_thread_to_cpu(int cpuid) {
    static int warned = 0;

    if (!warned) {
        warned = 1;
        fprintf(stderr, "WARNING: processor affinity not supported\n");
    }
}
#else
void bind_thread_to_cpu(int cpuid) {
    cpu_set_t mask;
    CPU_ZERO(&mask);

    CPU_SET(cpuid, &mask);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask)) {
        err(1, "sched_setaffinity");
    }
}
#endif
