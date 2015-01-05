/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "cwlock.h"

#include <limits.h>
#include <linux/futex.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "debug.h"
#include "util.h"

#define CWLOCK_LOCKED 0x01
#define CWLOCK_WAITER 0x02
#define CWLOCK_UNIT   0x04

void cwlock_init(cwlock_t *l) {
  l->value = 0;
}

bool cwlock_lock(cwlock_t *l) {
  uint32_t oldv;
  uint32_t newv;

  newv = ACCESS_ONCE(l->value);
  do {
    oldv = newv;

    if (!(oldv & CWLOCK_LOCKED)) {
      newv |= CWLOCK_LOCKED;
    } else if (!(oldv & CWLOCK_WAITER)) {
      newv |= CWLOCK_WAITER;
    } else {
      break;
    }

    /* Try to acquire the lock or flag that there's a waiter. */
    newv = __sync_val_compare_and_swap(&l->value, oldv, newv);
  } while (oldv != newv);

  /* If the old value doesn't have the lock bit, we acquired the lock. */
  if (!(oldv & CWLOCK_LOCKED)) {
    return true;
  }

  /* We didn't acquire the lock, so wait for it to be released. */
  oldv |= CWLOCK_LOCKED | CWLOCK_WAITER;
  do {
    syscall(SYS_futex, &l->value, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, oldv,
        NULL, NULL, 0);
    newv = ACCESS_ONCE(l->value);
  } while (newv == oldv);

  return false;
}

void cwlock_unlock(cwlock_t *l) {
  uint32_t oldv;
  uint32_t newv;

  /* Increment the generation and remove all flags. */
  newv = ACCESS_ONCE(l->value);
  do {
    oldv = newv;
    if (newv & CWLOCK_WAITER) {
      newv = (newv + CWLOCK_UNIT) & ~(CWLOCK_LOCKED | CWLOCK_WAITER);
    } else {
      newv &= ~CWLOCK_LOCKED;
    }
    newv = __sync_val_compare_and_swap(&l->value, oldv, newv);
  } while (oldv != newv);

  /* Wake up waiters if there are any. */
  if (oldv & CWLOCK_WAITER) {
    syscall(SYS_futex, &l->value, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT_MAX,
            NULL, NULL, 0);
  }
}
