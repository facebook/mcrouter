/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#ifndef FBI_COUNTER_H
#define FBI_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

#include "mcrouter/lib/fbi/decls.h"

__BEGIN_DECLS

/** A signed 63 bit thread-safe counter that dynamically trades space for
 *  contention tolerance.  Initialize to zero. */
typedef struct counter_s {
    int64_t data; /** n*2 if deflated, ptr+1 if inflated */
} counter_t;


/* After inflation, we want to be spread out onto 8 cache lines, each
   of which holds 8 int64_t. There is no need to separate padding from
   the actual values, so we just spread among all 64 locations. We want
   consecutive thread ids to go to different cache lines, though. */
#define COUNTER_LOG_INFLATED_SIZE 6
#define COUNTER_INFLATED_SIZE (1 << COUNTER_LOG_INFLATED_SIZE)

/** Number of failed CAS before inflation */
#define COUNTER_INFLATION_THRESHOLD 2

typedef struct counter_inflated_s {
    int64_t counts[COUNTER_INFLATED_SIZE];
} counter_inflated_t;


/** Holds COUNTER_INFLATED_SIZE+i, where i is the index into
 *  counter_inflated_t.counts[] that should be used by the current thread,
 *  or zero if never calculated.
 */
extern __thread int counter_slot_cache;


/** Returns an approximation of the current value of this counter.  The
 *  result will be exact if there are no concurrent calls to counter_add.
 */
int64_t counter_get(const counter_t *counter);


/** Initializes and returns the current thread's counter_slot_cache.  */
int counter_compute_slot();

/** Used by the inline counter_add if inflation is required. */
void counter_try_inflate(counter_t *counter);


/** Adjusts the value of the counter by delta. */
static inline void counter_add(counter_t *counter, int64_t delta) {
    int failures = 0;

    if (delta == 0) {
        return;
    }

    while (true) {
        const int64_t data = counter->data;

        if ((data & 1) != 0) {
            /* alread inflated */
            counter_inflated_t *f = (counter_inflated_t *)(data & ~1);
            int slot = counter_slot_cache;

            if (__builtin_expect(slot == 0, false)) {
                slot = counter_compute_slot();
            }
            __sync_add_and_fetch(
                    f->counts + (slot & (COUNTER_INFLATED_SIZE - 1)),
                    delta);
            return;
        }

        /* attempt non-inflated update */
        int64_t sum = (data >> 1) + delta;
        if (__builtin_expect(__sync_bool_compare_and_swap(
                        &counter->data, data, sum << 1), true)) {
            /* successful CAS */
            return;
        }

        if (++failures > COUNTER_INFLATION_THRESHOLD) {
            counter_try_inflate(counter);
        }
    }
}

/** Adjusts the value of the counter by delta, in a calling context
 *  in which the caller guarantees that there will be no concurrent
 *  calls to other counter_* methods except counter_get.  This method
 *  is intended for use in data structures where only some instances are
 *  used in a multi-threaded fashion (such as stats), so that all of the
 *  declarations may use counter_t without requiring all threads to pay
 *  the cost of thread safety.
 */
static inline void counter_add_nonlocked(counter_t *counter, int64_t delta) {
    const int64_t data = counter->data;
    const int64_t sum = (data >> 1) + delta;

    if (__builtin_expect((data & 1) != 0, false)) {
        /* alread inflated */
        counter_inflated_t *f = (counter_inflated_t *)(data & ~1);
        f->counts[0] += delta;
    } else {
        /* common case */
        counter->data = sum << 1;
    }
}

/** Ensures that this counter_t is bitwise copyable.  It is not safe to
 *  call this method concurrent with any other counter method (except
 *  counter_get), and its effect lasts only until another non-counter_get
 *  method is called. */
static inline void counter_deflate_nonlocked(counter_t *counter) {
    counter->data = counter_get(counter) << 1;
}


/** A hard limit on the number of counter_t inflations that will occur
 *  due to contention.  Since the memory devoted to inflated counters
 *  is never reclaimed, this puts an upper limit on the memory waste.
 *  In practice inflation is rare. */
#define MAX_COUNTER_INFLATIONS 1000000

/** Returns the number of inflations that have been performed across
 *  all counter_t. */
int counter_get_total_inflations();

__END_DECLS

#endif
