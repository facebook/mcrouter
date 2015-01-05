/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_LFCALLOC_H
#define FBI_LFCALLOC_H

#include <stddef.h>

/**
 * This file and its matching .c file contain the implementation of a Lock-Free
 * Chunk Allocator. It allows threads to concurrently allocate from a
 * contiguous block of memory; allocations are permanent in the sense that the
 * allocator doesn't have a free() method.
 *
 * It is useful as a building block for more sophisticated allocators like a
 * slab allocator: the LFCA can be used to allocate slabs into a class as well
 * as to allocate chunks within a recently-allocated slab.
 */

typedef struct {
  void *next;
  void *limit;
} lfchunk_allocator_t;

/**
 * This routine initializes the chunk allocator with the given buffer. It is
 * *not* safe to call this from different threads concurrently.
 */
void lfca_init(lfchunk_allocator_t *a, void *buf, size_t size);

/**
 * This routine re-initializes the buffer the allocator allocates from. It is
 * *not* safe to call it from different threads concurrently. It is, however,
 * safe to call lfca_alloc() concurrently.
 */
void lfca_reinit(lfchunk_allocator_t *a, void *buf, size_t size);

/**
 * This routine allocates a chunk from the given allocator. It is safe to call
 * this function from multiple threads concurrently (i.e., it doesn't need to
 * be protected by a lock).
 */
void *lfca_alloc(lfchunk_allocator_t *a, size_t size);

/**
 * This routine calculates how much space is left on the allocator, i.e., how
 * much can still be allocated from the allocator. This function can be called
 * concurrently from different threads, but the result may be immediately
 * obsolete, so it should only be used as a hint unless it is only used when
 * other calls to the allocator are known not to be happening in parallel.
 */
size_t lfca_space_left(lfchunk_allocator_t *a);

#endif
