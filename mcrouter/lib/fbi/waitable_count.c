/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "waitable_count.h"

#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

#define WAITABLE_COUNTER_STOPPED 0x80000000UL /* Count is stopped. */
#define WAITABLE_COUNTER_MAX     0x7FFFFFFFUL /* Maximum value of the count. */

/**
 * This routine initializes the counter to zero and sets up its maximum
 * value (i.e., the value above which the count won't be allowed to go).
 */
void waitable_counter_init(waitable_counter_t *wc, unsigned max) {
  wc->cnt = 0;
  wc->max = (max && max < WAITABLE_COUNTER_MAX) ? max : WAITABLE_COUNTER_MAX;
}

/**
 * This routine returns the current value in the counter.
 *
 * N.B. This value is supposed to be used as a hint only, given that this is a
 *      lock-free counter and can be modified by other threads.
 */
unsigned waitable_counter_count(waitable_counter_t *wc) {
  return *(unsigned volatile *)&wc->cnt & ~WAITABLE_COUNTER_STOPPED;
}

/**
 * This routine returns the current "stopped" state of the counter.
 *
 * N.B. When 'false' is returned, is must be treated as a hint only, as other
 *      threads may change this. On the other hand, a value of 'true' is
 *      sticky.
 */
bool waitable_counter_is_stopped(waitable_counter_t *wc) {
  return (*(unsigned volatile *)&wc->cnt & WAITABLE_COUNTER_STOPPED) != 0;
}

/**
 * This routine marks the given counter as 'stopped', which causes subsequent
 * attempts to increment the counter to fail.
 */
void waitable_counter_stop(waitable_counter_t *wc) {
  __sync_or_and_fetch(&wc->cnt, WAITABLE_COUNTER_STOPPED);
}

/**
 * This routine stops the given counter and, if needed, waits for the count to
 * drop to zero before returning.
 */
bool waitable_counter_wait(waitable_counter_t *wc, int timeout) {
  unsigned cnt;
  struct timespec ts;
  struct timespec *pts;
  int ret;

  if (timeout < 0) {
    pts = NULL;
  } else {
    pts = &ts;
    ts.tv_sec = timeout / 1000;
    ts.tv_nsec = (timeout % 1000) * 1000000;
  }

  cnt = __sync_or_and_fetch(&wc->cnt, WAITABLE_COUNTER_STOPPED);
  while (cnt != WAITABLE_COUNTER_STOPPED) {
    ret = syscall(SYS_futex, &wc->cnt, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, cnt,
                  pts, NULL, 0);
    if (ret == -1 && errno == ETIMEDOUT) {
      return false;
    }
    cnt = *(unsigned volatile *)&wc->cnt;
  }

  return true;
}

/**
 * This routine attempts to increment the given counter and returns the status
 * of the attempt. On swccess, the caller is reponsible for eventually
 * decrementing the counter by the same amount using one or more calls to
 * waitable_counter_dec().
 *
 * It will fail if the counter is stopped or if the increment would cause the
 * counter to go above its maximum.
 */
bool waitable_counter_inc(waitable_counter_t *wc, unsigned v) {
  unsigned latest;
  unsigned cnt;
  unsigned max;

  max = wc->max;

  latest = *(unsigned volatile *)&wc->cnt;
  do {
    cnt = latest;
    if ((cnt & WAITABLE_COUNTER_STOPPED) || (v > max - cnt)) {
      return false;
    }

    latest = __sync_val_compare_and_swap(&wc->cnt, cnt, cnt + v);
  } while (latest != cnt);

  return true;
}

/**
 * This routine decrements the given counter. If there are waiters, and the
 * count is decremented to zero, it will also wake up the waiters.
 */
void waitable_counter_dec(waitable_counter_t *wc, unsigned v) {
  unsigned cnt;

  cnt = __sync_sub_and_fetch(&wc->cnt, v);
  if (cnt == WAITABLE_COUNTER_STOPPED) {
    syscall(SYS_futex, &wc->cnt, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT_MAX,
            NULL, NULL, 0);
  }
}
