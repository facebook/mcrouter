/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "nstring.h"

#define fbi_nstring_alloc(s) malloc(s)
#define fbi_nstring_free(b) free(b)

size_t fbi_nstring_sizeof(const size_t len) {
  return nstring_sizeof(len);
}

nstring_t* nstring_new(const char* str, size_t len) {
  size_t size = sizeof(nstring_t) + len + 1;
  nstring_t* result;

  if ((result = (nstring_t*)fbi_nstring_alloc(size)) == NULL) {
    return NULL;
  }

  result->str = (char*)&result[1];
  result->len = len;
  memcpy(result->str,str, len);
  result->str[result->len] = '\0';
  return result;
}

void nstring_del(const nstring_t* val) {
  fbi_nstring_free((void*)val);
}

nstring_t* nstring_dup(const nstring_t* src) {
  size_t size = nstring_sizeof(src->len);
  nstring_t* result;

  if ((result = (nstring_t*)fbi_nstring_alloc(size)) == NULL) {
    return NULL;
  }
  result->str = (char*)&result[1];
  nstring_cpy(result, src);
  return result;
}



nstring_map_iter_t* nstring_map_iter_new(const nstring_map_t* map) {
  nstring_map_iter_t *iter = map->allocate(sizeof(nstring_map_iter_t));
  if (iter) {
    nstring_map_iter_init(map, iter);
  }
  return iter;
}

void nstring_map_iter_del(nstring_map_iter_t *iter) {
  if (iter) {
    iter->map->deallocate(iter);
  }
}

nstring_t* nstring_map_iter_get_key(nstring_map_iter_t *iter) {
  if (!iter->entry) {
    return NULL;
  }
  return &(iter->entry->key);
}


const void* nstring_map_iter_get_value(nstring_map_iter_t *iter) {
  if (!iter->entry) {
    return NULL;
  }
  return iter->entry->value;
}


//
// Redundant bodies for inline functions.
// These make the inline functions accessible in Python.
//

void fbi_nstring_cpy(nstring_t* dest, const nstring_t* src) {
  nstring_cpy(dest, src);
}


void fbi_nstring_copy(nstring_t* dest,
                      const nstring_t* src) {
  nstring_copy(dest, src);
}

int fbi_nstring_cmp(const nstring_t* a, const nstring_t* b) {
  return nstring_cmp(a, b);
}

int fbi_nstring_ncmp(const nstring_t* a,
                     const nstring_t* b,
                     const size_t len) {
  return nstring_ncmp(a, b, len);
}

nstring_t* fbi_nstring_dup(const nstring_t* a) {
  return nstring_dup(a);
}

const char* fbi_nstring_safe(const nstring_t* nstring) {
  return nstring_safe(nstring);
}

uint32_t fbi_nstring_hash(const nstring_t* key) {
  return nstring_hash(key);
}

size_t fbi_nstring_map_sizeof(const size_t buckets) {
  return nstring_map_sizeof(buckets);
}

void fbi_nstring_map_init(nstring_map_t* map,
                          const size_t buckets,
                          const uint32_t mask,
                          void* (*allocator)(const size_t),
                          void (*deallocator)(void*)) {
  return _nstring_map_init(map, buckets, mask, allocator, deallocator);
}


nstring_map_t* fbi_nstring_map_new(const size_t buckets,
                                   const uint32_t mask,
                                   void* (*allocator)(const size_t),
                                   void (*deallocator)(void*)) {
  return nstring_map_new(buckets, mask, allocator, deallocator);
}


void fbi_nstring_map_clear(nstring_map_t* map) {
  nstring_map_clear(map);
}


void fbi_nstring_map_del(nstring_map_t* map) {
  nstring_map_del(map);
}


size_t fbi_nstring_map_size(const nstring_map_t* map) {
  return nstring_map_size(map);
}


nstring_map_entry_t** fbi_nstring_map_prev(nstring_map_t* map,
                                           const nstring_t* key) {
  return nstring_map_prev(map, key);
}


int fbi_nstring_map_set(nstring_map_t* map,
                                const nstring_t* key,
                                const void* value,
                                const void** old_value) {
  return nstring_map_set(map, key, value, old_value);
}


const void* fbi_nstring_map_get(const nstring_map_t* map,
                            const nstring_t* key) {
  return nstring_map_get(map, key);
}


void fbi_nstring_map_remove(nstring_map_t* map,
                           const nstring_t* key,
                           const void **old_value) {
  nstring_map_remove(map, key, old_value);
}


void fbi_nstring_map_iter_init(const nstring_map_t* map,
                               nstring_map_iter_t *iter) {
  nstring_map_iter_init(map, iter);
}


int fbi_nstring_map_iter_is_valid(const nstring_map_iter_t* iter) {
  return nstring_map_iter_is_valid(iter);
}


int fbi_nstring_map_iter_has_next(const nstring_map_iter_t* iter) {
  return nstring_map_iter_has_next(iter);
}


nstring_map_entry_t* fbi_nstring_map_iter_next(nstring_map_iter_t *iter) {
  return nstring_map_iter_next(iter);
}
