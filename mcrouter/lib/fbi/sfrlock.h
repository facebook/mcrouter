/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_SFRLOCK_H
#define FBI_SFRLOCK_H
/**
 * This file and its matching .c file contain the implementation of a small
 * (8 bytes) non-recursive unfair read/write lock that is optimized for uses
 * when reads are common and writes less so.
 *
 * The state of the lock itself is fully described by sfrlock::value, which is
 * composed of two fields: a 1-bit "write locked" (W), and a 31-bit "reader
 * count" (R). In the description that follows, states are described as ordered
 * sets containing the values of W and R. The following are the valid states:
 *
 * 1. { 0, 0 } -- No one owns the lock.
 * 2. { 1, 0 } -- Writer owns the lock exclusively.
 * 3. { 0, N } -- N readers share ownership of the lock.
 * 4. { 1, N } -- N readers share ownership of the lock, but there's a writer
 *                waiting to take ownership.
 *
 * In addition to the state described above, the lock also contains a "waiter
 * count", which indicates to writers if they should make the futex wake system
 * call when the following transition just happened:
 *
 * { 1, 0 } -> { 0, 0 } -- that is, when the writer releases the lock.
 *
 * When readers release the lock, three different types of transitions can
 * happen:
 *
 * { X, N + 1 } -> { X, N } -- this is when a reader which isn't the last one
 * releases the lock. In this case the system call is never made because no
 * waiter would be able to make progress, in fact, only writers can be waiting.
 *
 * { 0, 1 } -> { 0, 0 } -- this is when the last reader releases the lock, but
 * there are no writers attempting to own the lock (and readers wouldn't wait,
 * they'd just acquire it), so the system call isn't made either.
 *
 * { 1, 1 } -> { 1, 0 } -- this is when the last reader releases the lock, and
 * there is a writer waiting. In this case the wake system call is made.
 *
 * N.B. The cost to perform a system call in the uncontended path is
 *      prohibitively high, that's why we only do it when there are waiters.
 *
 * Write lock uncontended acquisition:
 * { 0, 0 } -> { 1, 0 } -- Just flip the W bit to 1.
 *
 * Write lock contended acquistion:
 * { 0, N } -> { 1, N } -> { 1, N - 1 } -> ... -> { 1, 0 } -- in the presence
 * of N readers, the writer first sets the W bit (which prevents new readers
 * from acquiring the lock, and also keeps contending writers at bay), then
 * waits for all the readers to release the lock (i.e., wait for R to drop from
 * N to 0).
 *
 * { 1, N } -> ... -> { 0, 0 } -> { 1, 0 } -- in the presence of another writer
 * and potentially other readers, it first waits for everyone to release the
 * lock, then acquires it. It must be noted, however, that there is no
 * indication to potentially contending readers that there's a writer waiting,
 * so it is possible that a reader (or another writer for that matter) swoops
 * in and takes the lock before the current thread, in which case it will have
 * to retry one of the contended acquisition paths. The lock is unfair when
 * multiple writers are attempting to acquire it.
 *
 * Read lock uncontended acquisitions:
 * { 0, 0 } -> { 0, 1 } -- First reader.
 * { 0, N - 1 } -> { 0, N } -- N-th reader.
 *
 * Read lock contended acquisition:
 * { 1, 0 } -> { 0, 0 } -> { 0, 1 } -- waits for writer to release lock.
 * { 1, N } -> { 1, N - 1 } -> ... -> { 1, 0 } -> { 0, 0 } -> { 0, 1 } -- in
 * this case, a write is in the process of acquiring the lock, so we must
 * first wait for everyone to release it.
 */
#include <stdint.h>

#include "mcrouter/lib/fbi/util.h"

typedef struct {
  uint32_t value;
  uint32_t waiters;
} sfrlock_t;

#define SFRLOCK_WRITE_LOCKED 0x80000000U

__BEGIN_DECLS
void sfrlock_init(sfrlock_t *l);
void sfrlock_rdlock_contended(sfrlock_t *l);
void sfrlock_wrlock_contended(sfrlock_t *l);
void sfrlock_wake_waiters(sfrlock_t *l);

static inline void sfrlock_rdlock(sfrlock_t *lock) {
//we may want to rewrite this just using asm for clang
#if __x86_64__ && !SFRLOCK_FORCE_INTRINSICS && !__clang__
  asm volatile goto ("movl %0, %%eax;"        /* Read the lock value. */
                "1: testl %%eax, %%eax;"
                "js 1f;"                      /* If the most significant bit is
                                               * set, there is a writer so we
                                               * must take the contended path.
                                               */
                "lea 0x1(%%rax), %%rdi;"      /* Otherwise, store in rdi the
                                               * current value of lock plus one.
                                               */
                "lock cmpxchgl %%edi, %0;"    /* Atomically try to store the
                                               * new value on the lock. It only
                                               * succeeds if the value in the
                                               * lock is still what's in eax.
                                               */
                "jz %l[exit];"                /* If the store succeeded, the
                                               * lock is now acquired and we
                                               * can return to the caller.
                                               */
                "jmp 1b;"                     /* Otherwise, we have the new
                                               * value in eax, so try again
                                               * until we suceeed or a writer
                                               * shows up.
                                               */
                "1:"
                :
                : "m" (lock->value)
                : "rax", "rdi", "memory", "cc"
                : exit);

  sfrlock_rdlock_contended(lock);
exit:
  return;
#else
  uint32_t oldv;
  uint32_t newv;

  newv = ACCESS_ONCE(lock->value);
  do {
    oldv = newv;
    if (oldv & SFRLOCK_WRITE_LOCKED) {
      return sfrlock_rdlock_contended(lock);
    }
    newv = __sync_val_compare_and_swap(&lock->value, oldv, oldv + 1);
  } while (newv != oldv);
#endif
}

static inline void sfrlock_rdunlock(sfrlock_t *lock) {
#if __x86_64__ && !SFRLOCK_FORCE_INTRINSICS && !__clang__
  asm volatile goto ("xorl %%edi, %%edi;"   /* Zero-out edi. */
                "decl %%edi;"               /* Decrement edi, making it -1. */
                "lock xaddl %%edi, %0;"     /* Add edi to the current value of
                                             * the lock. The old value is
                                             * stored in back in edi.
                                             */
                "dec %%edi;"                 /* Decrement edi, to get the value
                                              * we actually stored in the lock.
                                              */
                "jns %l[exit];"              /* If the most significant bit is
                                              * not set, there is no writer
                                              * attempting to acquire the lock,
                                              * so we don't need wake anyone.
                                              */
                "shl %%edi;"                 /* Shift the lock value to the
                                              * left to throw away the most
                                              * significant bit.
                                              */
                "jnz %l[exit];"               /* If the value is non-zero, we
                                               * are not the last reader, so
                                               * we must not wake up the writer
                                               * just yet.
                                               */
                :
                : "m" (lock->value)
                : "rdi", "memory", "cc"
                : exit);

  sfrlock_wake_waiters(lock);
exit:
  return;
#else
  if (__sync_sub_and_fetch(&lock->value, 1) == SFRLOCK_WRITE_LOCKED) {
    sfrlock_wake_waiters(lock);
  }
#endif
}

static inline void sfrlock_wrlock(sfrlock_t *lock) {
#if __x86_64__ && !SFRLOCK_FORCE_INTRINSICS && !__clang__
  asm volatile goto ("xorl %%eax, %%eax;"     /* Zero-out eax. */
                "movl $0x80000000, %%edi;"    /* Set edi to the lock state
                                               * where there are no readers
                                               * and a writer.
                                               */
                "lock cmpxchgl %%edi, %0;"    /* Atomically try to set the lock
                                               * value, it only succeeds if the
                                               * lock value is zero (eax).
                                               */
                "jz %l[exit];"                /* If we succeeded, the lock is
                                               * acquired and we can skip the
                                               * uncontended path.
                                               */
                :
                : "m" (lock->value)
                : "rax", "rdi", "memory", "cc"
                : exit);

  sfrlock_wrlock_contended(lock);
exit:
  return;
#else
  if (!__sync_bool_compare_and_swap(&lock->value, 0, SFRLOCK_WRITE_LOCKED)) {
    sfrlock_wrlock_contended(lock);
  }
#endif
}

static inline void sfrlock_wrunlock(sfrlock_t *lock) {
#if __x86_64__ && !SFRLOCK_FORCE_INTRINSICS && !__clang__
  asm volatile goto ("xorl %%edi, %%edi;"  /* Zero-out edi. */
                "xchgl %%edi, %0;"         /* Store edi in the lock, which in
                                            * effect, releases it.
                                            */
                "mov %1, %%edi;"           /* Read the waiter count. */
                "test %%edi, %%edi;"
                "jz %l[exit];"             /* If there are no waiters, just
                                            * skip the wake up system call.
                                            */
                :
                : "m" (lock->value), "m" (lock->waiters)
                : "rdi", "memory", "cc"
                : exit);

  sfrlock_wake_waiters(lock);
exit:
  return;
#else
  /*
   * We could have used __sync_lock_release below, but GCC only guarantees a
   * release-barrier, which isn't enough because we have to make sure the read
   * of lock->waiters doesn't leak into the critical section.
   */
  __sync_bool_compare_and_swap(&lock->value, SFRLOCK_WRITE_LOCKED, 0);
  if (ACCESS_ONCE(lock->waiters)) {
    return sfrlock_wake_waiters(lock);
  }
#endif
}

__END_DECLS

#endif
