/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_SKIPLIST_H
#define FBI_SKIPLIST_H

#include <stdint.h>

#include "mcrouter/lib/fbi/decls.h"

__BEGIN_DECLS

#define skiplist_alloc(s) malloc(s)
#define skiplist_free(b) free(b)

typedef enum {
  SKIPLIST_OK,
  SKIPLIST_MEM_EXHAUSTED,
  SKIPLIST_DUPLICATE_KEY,
  SKIPLIST_KEY_NOT_FOUND,
  SKIPLIST_UNKNOWN_ERROR,
} skiplist_status_t;

typedef struct skiplist_node_s {
  uint32_t key;
  const void* rec;
  /* skip list forward pointer */
  struct skiplist_node_s* forward[1];
} skiplist_node_t;

/* implementation independent declarations */
typedef struct skiplist_s {
  skiplist_node_t *hdr;
  int level;
} skiplist_t;

skiplist_status_t skiplist_insert(skiplist_t* list,
                                  const uint32_t key,
                                  const void* const rec);

skiplist_status_t skiplist_delete(skiplist_t* list,
                                  uint32_t key);

skiplist_status_t skiplist_find(const skiplist_t* list,
                                       uint32_t key,
                                       const void** rec);

skiplist_status_t skiplist_findnextlargest(const skiplist_t* list,
                                           const uint32_t key,
                                           const void** rec);

skiplist_t* skiplist_new();

void skiplist_del(skiplist_t* skiplist);

__END_DECLS

#endif
