/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "lfcalloc.h"

#include <stddef.h>

#include "util.h"

void lfca_init(lfchunk_allocator_t *a, void *buf, size_t size) {
  a->next = buf;
  a->limit = buf + size;
}

void lfca_reinit(lfchunk_allocator_t *a, void *buf, size_t size) {
  a->limit = NULL;
  __sync_synchronize();
  a->next = buf;
  __sync_synchronize();
  a->limit = buf + size;
}

void *lfca_alloc(lfchunk_allocator_t *a, size_t size) {
  void *newv;
  void *oldv;

  newv = ACCESS_ONCE(a->next);
  do {
    oldv = newv;
    newv += size;
    if (newv > ACCESS_ONCE(a->limit)) {
      return NULL;
    }

    newv = __sync_val_compare_and_swap(&a->next, oldv, newv);
  } while (oldv != newv);

  return oldv;
}

size_t lfca_space_left(lfchunk_allocator_t *a) {
  lfchunk_allocator_t cap;

  cap = ACCESS_ONCE(*a);
  if (cap.next > cap.limit) {
    return 0;
  }

  return cap.limit - cap.next;
}
