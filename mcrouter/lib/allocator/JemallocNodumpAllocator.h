/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#if !defined(FOLLY_SANITIZE_ADDRESS) && \
  (JEMALLOC_VERSION_MAJOR > 3) && \
  defined(MADV_DONTDUMP)
  #define CAN_USE_JEMALLOC_NODUMP_ALLOCATOR
#endif

#ifdef CAN_USE_JEMALLOC_NODUMP_ALLOCATOR

#include <jemalloc/jemalloc.h>
#include <sys/mman.h>

namespace facebook { namespace memcache {

/**
 * An allocator which uses Jemalloc to create an dedicated arena to allocate
 * memory from. The only special property set on the allocated memory is that
 * the memory is not dump-able. This is done by setting MADV_DONTDUMP using the
 * `madvise` system call. A custom hook installed which is called when
 * allocating a new chunk of memory. All it does is call the original jemalloc
 * hook to allocate the memory and then set the advise on it before returning
 * the pointer to the allocated memory. Jemalloc does not use allocated chunks
 * across different arenas, without `munmap`-ing them first, and the advises are
 * not sticky i.e. they are unset if `munmap` is done. Also this arena can't be
 * used by any other part of the code by just calling `malloc`.
 */
class JemallocNodumpAllocator {
 public:
  JemallocNodumpAllocator();
  ~JemallocNodumpAllocator();

  void* allocate(const size_t size);
  void* reallocate(void* p, const size_t size);
  void deallocate(void* p);

  static void deallocate(void* p, void* userData);
  unsigned getArenaIndex() const { return arena_index_; }
  int getFlags() const { return flags_; }

  static chunk_alloc_t* getOriginalChunkAlloc() {
    return original_chunk_alloc_;
  }

 private:
  static chunk_alloc_t* original_chunk_alloc_;
  static void* chunk_alloc(void* chunk,
                           size_t size,
                           size_t alignment,
                           bool* zero,
                           bool* commit,
                           unsigned arena_ind);

  void extend_and_setup_arena();

  unsigned arena_index_{0};
  int flags_{0};
};

}}  // facebook::memcache

#endif
