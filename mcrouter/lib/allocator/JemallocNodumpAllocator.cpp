/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "JemallocNodumpAllocator.h"

#ifdef CAN_USE_JEMALLOC_NODUMP_ALLOCATOR

#include <array>
#include <folly/Conv.h>
#include <glog/logging.h>

namespace facebook { namespace memcache {

chunk_alloc_t* JemallocNodumpAllocator::original_chunk_alloc_ = {nullptr};

JemallocNodumpAllocator::JemallocNodumpAllocator() {
  extend_and_setup_arena();
  LOG(INFO) << "Set up arena: " << arena_index_;
}

void JemallocNodumpAllocator::extend_and_setup_arena() {
    size_t len = sizeof(arena_index_);
    int retval = mallctl("arenas.extend", &arena_index_, &len, nullptr, 0);
    if (retval != 0) {
      std::array<char, 128> buf;
      LOG(FATAL) << "Unable to extend arena: "
                 << strerror_r(retval, buf.data(), buf.size());
      return;
    }
    flags_ = MALLOCX_ARENA(arena_index_) | MALLOCX_TCACHE_NONE;

    // Set the custom alloc hook
    const auto str = folly::to<std::string>("arena.",
                                            arena_index_,
                                            ".chunk_hooks");
    chunk_hooks_t hooks;
    len = sizeof(hooks);
    // Read the existing hooks
    retval = mallctl(str.c_str(), &hooks, &len, nullptr, 0);
    if (retval != 0) {
      std::array<char, 128> buf;
      LOG(FATAL) << "Unable to get the hooks: "
                 << strerror_r(retval, buf.data(), buf.size());
      return;
    }
    if (nullptr == original_chunk_alloc_) {
      original_chunk_alloc_ = hooks.alloc;
    } else {
      DCHECK(original_chunk_alloc_ == hooks.alloc);
    }

    // Set the custom hook
    hooks.alloc = &JemallocNodumpAllocator::chunk_alloc;
    retval = mallctl(str.c_str(), nullptr, nullptr, &hooks, sizeof(hooks));
    if (retval != 0) {
      std::array<char, 128> buf;
      LOG(FATAL) << "Unable to set the hooks: "
                 << strerror_r(retval, buf.data(), buf.size());
      return;
    }
}

JemallocNodumpAllocator::~JemallocNodumpAllocator() {
}

void* JemallocNodumpAllocator::allocate(const size_t size) {
  return mallocx(size, flags_);
}

void* JemallocNodumpAllocator::reallocate(void* p, const size_t size) {
  return rallocx(p, size, flags_);
}

void* JemallocNodumpAllocator::chunk_alloc(void* chunk,
                                           size_t size,
                                           size_t alignment,
                                           bool* zero,
                                           bool* commit,
                                           unsigned arena_ind) {
  void* retval = original_chunk_alloc_(chunk,
                                       size,
                                       alignment,
                                       zero,
                                       commit,
                                       arena_ind);
  if (retval != nullptr) {
    const int rc = madvise(retval, size, MADV_DONTDUMP);
    if (rc) {
      std::array<char, 128> buf;
      LOG(WARNING) << "Unable to set MADV_DONTDUMPT on : " << retval << ", "
                   << strerror_r(rc, buf.data(), buf.size());
    }
  }

  return retval;
}

void JemallocNodumpAllocator::deallocate(void* p) {
  dallocx(p, flags_);
}

void JemallocNodumpAllocator::deallocate(void* p, void* userData) {
  const uint64_t flags = reinterpret_cast<uint64_t> (userData);
  dallocx(p, flags);
}

}}  // facebook::memcache

#endif
