/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "sfrlock.h"

#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "debug.h"
#include "util.h"

void sfrlock_init(sfrlock_t *l) {
  l->value = 0;
  l->waiters = 0;
}

void sfrlock_rdlock_contended(sfrlock_t *l) {
  uint32_t oldv;
  uint32_t newv;

  __sync_fetch_and_add(&l->waiters, 1);

  newv = ACCESS_ONCE(l->value);
  do {
    oldv = newv;

    /* Wait for the write lock to be released. */
    while (oldv & SFRLOCK_WRITE_LOCKED) {
      syscall(SYS_futex, &l->value, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, oldv,
              NULL, NULL, 0);
      oldv = ACCESS_ONCE(l->value);
    }

    /* Try to increment the reader count. */
    newv = __sync_val_compare_and_swap(&l->value, oldv, oldv + 1);
  } while (oldv != newv);

  __sync_fetch_and_sub(&l->waiters, 1);
}

void sfrlock_wake_waiters(sfrlock_t *l) {
  syscall(SYS_futex, &l->value, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT_MAX,
          NULL, NULL, 0);
}

void sfrlock_wrlock_contended(sfrlock_t *l) {
  uint32_t oldv;
  uint32_t newv;

  __sync_fetch_and_add(&l->waiters, 1);

  /*
   * The block below is very similar to the read lock acquisition, except that
   * instead of incrementing the read count, it sets the write lock bit once
   * the potential current writer is gone (i.e., when the write lock is not
   * held).
   */
  newv = ACCESS_ONCE(l->value);
  do {
    oldv = newv;

    /* Wait for the write lock to be released. */
    while (oldv & SFRLOCK_WRITE_LOCKED) {
      syscall(SYS_futex, &l->value, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, oldv,
              NULL, NULL, 0);
      oldv = ACCESS_ONCE(l->value);
    }

    /* Try to take the write lock. */
    newv = __sync_val_compare_and_swap(&l->value, oldv,
                                       oldv | SFRLOCK_WRITE_LOCKED);
  } while (oldv != newv);

  /*
   * We own the write lock. Now we just have to wait any potential readers to
   * release the lock before we can continue.
   */
  oldv |= SFRLOCK_WRITE_LOCKED;
  while (oldv != SFRLOCK_WRITE_LOCKED) {
    syscall(SYS_futex, &l->value, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, oldv,
            NULL, NULL, 0);
    oldv = ACCESS_ONCE(l->value);
  }

  __sync_fetch_and_sub(&l->waiters, 1);
}
