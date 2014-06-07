/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#ifndef FBI_UTIL_H
#define FBI_UTIL_H

#include <netinet/in.h>

#include "decls.h"

#define fbi_expected(__expr) __builtin_expect(!!(__expr), true)
#define fbi_unexpected(__expr) __builtin_expect(!!(__expr), false)

#define fbi_futex_wait(p, val)                                          \
  syscall(SYS_futex, (p), FUTEX_WAIT | FUTEX_PRIVATE_FLAG, (val),       \
          NULL, NULL, 0);

#define fbi_futex_wake(p, n)                                            \
  syscall(SYS_futex, (p), FUTEX_WAKE | FUTEX_PRIVATE_FLAG, (n),         \
          NULL, NULL, 0);

#ifndef ACCESS_ONCE
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))
#endif

__BEGIN_DECLS

static inline uint32_t xchg32_barrier(volatile uint32_t *ptr, uint32_t value) {
#if defined(__x86_64__) && !defined(FBI_FORCE_INTRINSICS)
    asm volatile ("xchgl %0, %1"
                  : "+r" (value), "+m" (*ptr)
                  :
                  : "memory");
    return value;
#else
    __sync_synchronize();
    /* Following is only a partial barrier so keep __sync_synchronize above. */
    return __sync_lock_test_and_set(ptr, value);
#endif
}

/** compute the next highest power of 2 of 32-bit v.
    Will overflow if v > 2^31. */
static inline uint32_t next_pow2(uint32_t v) {
  // http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  v += (v == 0);
  return v;
}

static inline uint64_t fbi_swap_uint64_t(uint64_t u64) {
  union {
    uint64_t u64;
    struct {
      uint32_t high;
      uint32_t low;
    } u32;
  } swapper;
  int32_t swap;

  swapper.u64 = u64;
  swap = swapper.u32.high;
  swapper.u32.high = htonl(swapper.u32.low);
  swapper.u32.low = htonl(swap);
  return swapper.u64;
}

static inline uint64_t fbi_htonll(uint64_t u64) {
#if defined(__BYTE_ORDER) && defined(__BIG_ENDIAN) && defined(__LITTLE_ENDIAN)
  #if __BYTE_ORDER == __BIG_ENDIAN
  /* big-endian */
  int swapped = 0;
  #else
  /* little-endian */
  int swapped = 1;
  #endif /* LIBFBI_LITTLE_ENDIAN */
#else
  /* determine at runtime */
  int _one = 1;
  char *_p = (char *)&_one;
  int swapped = *_p;
#endif /* defined(__BYTE_ORDER) && ... */

  if (swapped) {
    return fbi_swap_uint64_t(u64);
  }

  return u64;
}

#define fbi_htonll(u) fbi_htonll(u)

__END_DECLS

#endif
