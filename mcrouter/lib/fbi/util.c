/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "util.h"

#include <stdint.h>

/* These functions are only used for testing. */

uint32_t fbi_test_next_pow2(uint32_t u32) {
  return next_pow2(u32);
}

uint64_t fbi_test_swap_uint64_t(uint64_t u64) {
  return fbi_swap_uint64_t(u64);
}

uint64_t fbi_test_htonll(uint64_t u64) {
  return fbi_htonll(u64);
}
