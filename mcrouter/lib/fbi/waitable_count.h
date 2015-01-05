/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_WAITABLE_COUNT_H
#define FBI_WAITABLE_COUNT_H

#include <stdbool.h>

/**
 * This file and its matching .c file contain the implementation of a waitable
 * counter that can be used to gate access to resources. It allows threads to
 * concurrently increment/decrement a counter, while allowing other threads to
 * wait for the counter to drop back to zero.
 *
 * Its expected use is as follows: threads wanting to use a resource would try
 * to increment the counter, and only use the resource if it succeeds, similar
 * to this:
 *
 *   if (waitable_counter_inc(&ctr, 1)) {
 *     [Do something with resource.]
 *     waitable_counter_dec(&ctr, 1);
 *   }
 *
 * While a thread that intends to destroy the resource would do the following:
 *
 *   waitable_counter_stop(&ctr);
 *   waitable_counter_wait(&ctr, timeout);
 *
 * The first call would prevent new users from starting to use the resource and
 * the second call would cause the thread to wait until all users are done with
 * the resource (i.e., the count gets to zero).
 */

typedef struct waitable_counter {
  unsigned cnt;
  unsigned max;
} waitable_counter_t;

void waitable_counter_init(waitable_counter_t *wc, unsigned max);
unsigned waitable_counter_count(waitable_counter_t *wc);
bool waitable_counter_is_stopped(waitable_counter_t *wc);
void waitable_counter_stop(waitable_counter_t *wc);
bool waitable_counter_wait(waitable_counter_t *wc, int timeout);
bool waitable_counter_inc(waitable_counter_t *wc, unsigned v);
void waitable_counter_dec(waitable_counter_t *wc, unsigned v);

#endif
