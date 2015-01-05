/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_CWLOCK_H
#define FBI_CWLOCK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * This file and its matching .c file contain the implementation of a small
 * (4 bytes) "common-work" lock.
 *
 * This lock is for use when potentially multiple threads need some common work
 * performed, but that work is exactly the same and can be shared. For example,
 * replenishing buffers in a lock-free free-list:
 *
 * for (;;) {
 *    ptr = free_list_pop(&free_list);
 *    if (ptr) {
 *        return ptr;
 *    }
 *
 *    if (cwlock_lock(&replenish_cw)) {
 *        ptr = replish_return_one(&free_list);
 *        cwlock_unlock(&replenish_cw);
 *
 *        return ptr;
 *    }
 * }
 *
 * When N threads attempt to acquire the lock concurrently, one of them will
 * succeed in acquiring the lock and will get a result of "true". The other
 * N-1 threads will block until the winner releases the lock, at which point
 * the N-1 threads will be released and the cwlock_lock() call will return
 * "false".
 *
 * Like conventional locks, only one thread can be in the critical region at
 * a time. Unlike conventional locks though, once the owner releases the lock,
 * all threads resume execution (but outside the critical region).
 *
 * The state of the lock is fully described by cwlock::value, which is composed
 * of three fields: a 1-bit "locked" (L), a 1-bit "waiter present" (W) and a
 * 30-bit "generation" (G). In the description that follows, states are
 * described as ordered sets containing the values of L, W & G. The following
 * are the valid states:
 *
 * 1. { 0, 0, N } -- No one owns the lock.
 * 2. { 1, 0, N } -- A thread owns the lock, but no threads are waiting.
 * 3. { 1, 1, N } -- A thread owns the lock, at least one other thread is
 *                   waiting for it to be released.
 *
 * The following transitions are valid when acquiring a lock:
 *
 * { 0, 0, N } -> { 1, 0, N } -- that is, the lock was not held prior to the
 * this attempt, so the caller becomes the owner.
 *
 * { 1, 0, N } -> { 1, 1, N } -- that is, the lock was already taken, so the
 * caller sets the "waiter present" bit and waits for the lock to be released.
 *
 * { 1, 1, N } -> { 1, 1, N } -- that is, the lock was already taken and there
 * was already a waiter, so the caller just waits for the lock to be released.
 *
 * N.B. For the last two cases, the caller will be informed that another thread
 *      has performed the work.
 *
 * The following transitions are valid when releasing the lock:
 *
 * { 1, 0, N } -> { 0, 0, N } -- that is, the lock bit is reset and no wake
 * system call is made because there were no waiters.
 *
 * { 1, 1, N } -> { 0, 0, N+1 } -- that is, the lock and waiter-present bits
 * are reset, the generation is incremented, and waiters are released. The
 * reason for the generation increment is so that the waiters, when they wake
 * up, will realize that the lock has already been released, even if in the
 * meantime other threads come along and set the L/W bits; in this case the
 * generation would still be different, so waiters would know they can come
 * out of the wait state.
 */

typedef struct {
  uint32_t value;
} cwlock_t;

__BEGIN_DECLS
void cwlock_init(cwlock_t *l);
bool cwlock_lock(cwlock_t *l);
void cwlock_unlock(cwlock_t *l);
__END_DECLS

#endif
