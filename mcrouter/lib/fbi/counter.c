/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "counter.h"
#include "debug.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


/** Used to assign thread ids */
static volatile int slot_seed = 0;

/** Incremented during each attempted inflation, may exceed
 *  MAX_COUNTER_INFLATIONS by the number of threads */
static volatile int total_inflations = 0;

/* See counter.h */
__thread int counter_slot_cache;


/** Returns the sum of the children counts of inflated */
static inline int64_t get_inflated(const counter_inflated_t *inflated) {
    int64_t sum = 0;
    int i;
    for (i = 0; i < COUNTER_INFLATED_SIZE; ++i) {
        sum += inflated->counts[i];
    }
    /* chop at 63 bits and then sign extend */
    return (sum << 1) >> 1;
}

/* See counter.h */
int64_t counter_get(const counter_t *counter) {
    int64_t d = counter->data;
    if ((d & 1) == 0) {
        /* we are careful to ensure sign extension here */
        return d >> 1;
    } else {
        counter_inflated_t *p = (counter_inflated_t *)(d & ~1);
        return get_inflated(p);
    }
}


/* See counter.h */
int counter_compute_slot() {
    /* we use the slot bits in reverse order so that we will spread
       among cache lines first, always setting at least one bit */
    int bits = __sync_add_and_fetch(&slot_seed, 1);
    int i;
    int slot = 1;
    for (i = 0; i < COUNTER_LOG_INFLATED_SIZE; ++i) {
        slot <<= 1;
        slot |= (bits & 1);
        bits >>= 1;
    }
    counter_slot_cache = slot;
    return slot;
}


/* See counter.h */
void counter_try_inflate(counter_t *counter) {
    if (total_inflations >= MAX_COUNTER_INFLATIONS) {
        /* nothing to do */
        return;
    }

    int64_t data = counter->data;
    void *p;
    counter_inflated_t *f;

    if ((data & 1) != 0) {
        /* already inflated, no need to complete here */
        return;
    }

    if ((p = calloc(1, sizeof(counter_inflated_t) + 7)) == NULL) {
        /* no memory, caller will keep CASing the uninflated counter */
        return;
    }

    /* exact total_inflations check */
    if (__sync_fetch_and_add(&total_inflations, 1) > MAX_COUNTER_INFLATIONS) {
        /* bail */
        free(p);
        return;
    }

    /* 8-byte align f within p */
    f = (counter_inflated_t *)((7 + (uintptr_t)p) & ~(uintptr_t)7);
    FBI_ASSERT(((char*)f - (char*)p) >= 0);
    FBI_ASSERT(((char*)f - (char*)p) < 8);

    /* install inflated counter */
    if (!__sync_bool_compare_and_swap(&counter->data, data,
                1 + (int64_t)f)) {
        /* failure, p is not in use */
        free(p);
    }

    /* regardless of success or failure, the caller will keep trying */
}

/* see counter.h */
int counter_get_total_inflations() {
    return total_inflations;
}
