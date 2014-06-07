/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

namespace facebook { namespace memcache {

/**
 * Stack allocator that protects an extra memory page after
 * the end of the stack.
 */
class GuardPageAllocator {
 public:
  inline unsigned char* allocate(size_t size);
  inline void deallocate(unsigned char* up, size_t size);
};

}}  // facebook::memcache

#include "GuardPageAllocator-inl.h"
