/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "counting_sem.h"

#include <limits.h>
#include <linux/futex.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "util.h"

#define fbi_futex_wait(p, val)                                          \
  syscall(SYS_futex, (p), FUTEX_WAIT | FUTEX_PRIVATE_FLAG, (val),       \
          NULL, NULL, 0);

#define fbi_futex_wake(p, n)                                            \
  syscall(SYS_futex, (p), FUTEX_WAKE | FUTEX_PRIVATE_FLAG, (n),         \
          NULL, NULL, 0);

#define MIN(a, b) ((a) <= (b) ? (a) : (b))
#define MAX(a, b) ((a) >= (b) ? (a) : (b))

void counting_sem_init(counting_sem_t* sem, int32_t val) {
  sem->cnt = MAX(val, 0);
}

int32_t counting_sem_value(counting_sem_t* sem) {
  int32_t cnt = ACCESS_ONCE(sem->cnt);
  return MAX(cnt, 0);
}

static int32_t counting_sem_lazy_helper(counting_sem_t* sem, int32_t n,
                                        bool nonblocking) {
  int32_t latest, prev, attempt, next;

  if (n <= 0) {
    return 0;
  }

  /*
   * Non-blocking case: semaphore value is positive.
   * Decrement it by at most n and return right away.
   */
  latest = ACCESS_ONCE(sem->cnt);
  while (latest > 0) {
    prev = latest;
    attempt = MIN(n, prev);
    latest = __sync_val_compare_and_swap(&sem->cnt, prev, prev - attempt);
    if (latest == prev) {
      return attempt;
    }
  }

  if (nonblocking) {
    return 0;
  }

  /*
   * Otherwise we have to wait and try again.
   */
  do {

    /* Wait loop */
    do {
      /*
       * Change 0 into -1.  Note we must do this check
       * every loop iteration due to the following scenario:
       * This thread sets -1.  Before we called wait, another thread
       * posts() and yet another thread waits() so the counter is back to 0.
       */
      if (latest == 0) {
        latest = __sync_val_compare_and_swap(&sem->cnt, 0, -1);
      }

      if (latest <= 0) {
        /*
         * Either we saw a 0 (and we set it to -1) or we saw a -1.
         * Wait if it's still a -1.
         */
        fbi_futex_wait(&sem->cnt, -1);
        latest = ACCESS_ONCE(sem->cnt);
      }
    } while (latest <= 0);

    /* latest > 0 due to loop above, so attempt is always positive */
    prev = latest;
    attempt = MIN(n, prev);
    next = prev - attempt;

    /*
     * Other threads might already be waiting.
     * We can't set this to 0 here, or post() will never wake them up.
     */
    if (next == 0) {
      next = -1;
    }
    latest = __sync_val_compare_and_swap(&sem->cnt, prev, next);
  } while (latest != prev);

  if (next > 0) {
    /*
     * The semaphore value is still positive.
     * We must wake here in case other threads are waiting.
     */
    fbi_futex_wake(&sem->cnt, 1);
  }

  return attempt;
}

int32_t counting_sem_lazy_wait(counting_sem_t* sem, int32_t n) {
  return counting_sem_lazy_helper(sem, n, false);
}

int32_t counting_sem_lazy_nonblocking(counting_sem_t* sem, int32_t n) {
  return counting_sem_lazy_helper(sem, n, true);
}


void counting_sem_post(counting_sem_t* sem, int32_t n) {
  int32_t latest, prev, base, next;

  if (n <= 0) {
    return;
  }

  latest = ACCESS_ONCE(sem->cnt);
  do {
    prev = latest;
    base = MAX(prev, 0);
    next = base + MIN(n, INT32_MAX - base);
    latest = __sync_val_compare_and_swap(&sem->cnt, prev, next);
  } while (latest != prev);

  if (prev < 0) {
    /* If we went out of the negative state, we need to wake a thread up. */
    fbi_futex_wake(&sem->cnt, 1);
  }
}
