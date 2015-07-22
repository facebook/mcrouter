/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_NSTRING_H
#define FBI_NSTRING_H
/** A collection of fixed length string utility functions.  */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mcrouter/lib/fbi/decls.h"

__BEGIN_DECLS

#define NSTRING_LIT(lit) {(char*)lit, sizeof(lit)-1}
#define NSTRING_INIT(cs) {cs, strlen(cs)}

/** @deprecated, for compatibility only */
#define NSTRING_CONST(cs) NSTRING_INIT(cs)

/** The nstring object - a tuple of char pointer and length */

typedef struct nstring_s {
  char* str;
  size_t len;
} nstring_t;

static inline size_t nstring_sizeof(const size_t len) {
  return sizeof(nstring_t) + len + 1;
}

/** this expects the destination len to already be set to max size */
static inline void nstring_ncpy(nstring_t* dest, const nstring_t* src) {
  memcpy(dest->str, src->str, dest->len);
  dest->str[dest->len] = '\0';
}

/** will copy src->len bytes; ensure dest has enough space! */
static inline void nstring_cpy(nstring_t* dest, const nstring_t* src) {
  dest->len = src->len;
  nstring_ncpy(dest, src);
}

/** Create an nstring object that references an existing string. No copying is
    done; you are responsible for the longevity of str while this nstring is
    in use. (Use when NSTRING_INIT isn't available, i.e. other than
    initialization time.) */
static inline nstring_t nstring_of(char* str) {
  nstring_t value = NSTRING_INIT(str);
  return value;
}

nstring_t* nstring_new(const char* str, size_t len);

void nstring_del(const nstring_t* val);

nstring_t* nstring_dup(const nstring_t* src);

static inline void nstring_copy(nstring_t* dest,
                                const nstring_t* src) {
  dest->len = src->len;
  if (dest->len > 0) {
    memcpy(dest->str, src->str, src->len);
    dest->str[dest->len] = '\0';
  }
}

static inline int nstring_cmp(const nstring_t* a, const nstring_t* b) {
  int result = a->len - b->len;
  if (result == 0) {
    result = memcmp(a->str, b->str, a->len);
  }
  return result;
}

static inline int nstring_ncmp(const nstring_t* a,
                               const nstring_t* b,
                               const size_t len) {
  return strncmp(a->str, b->str, len);
}

static inline const char* nstring_safe(const nstring_t* nstring) {
  if (nstring == NULL || nstring->str == NULL) {
    return "<NULL>";
  } else if (nstring->len < 1) {
    return "";
  } else {
    return nstring->str;
  }
}

/** Quick and dirty bernstein hash...fine for short ascii strings */

static inline uint32_t bernstein_hash(const char* str, size_t len) {
  uint32_t hash = 5381;
  size_t ix;

  for (ix = 0; ix < len; ix++) {
    hash = ((hash << 5) + hash) + str[ix]; /* hash * 33 + c */
  }

  return hash;
}

#define nstring_hash(x) bernstein_hash((x)->str, (x)->len)

typedef struct nstring_map_entry_s {
  nstring_t key;
  const void* value;
  struct nstring_map_entry_s* next;
} nstring_map_entry_t;

typedef struct nstring_map_s {
  nstring_map_entry_t** heads;
  size_t count;
  size_t buckets;
  void* (*allocate)(const size_t);
  void (*deallocate)(void*);
} nstring_map_t;

static inline size_t nstring_map_sizeof(const size_t buckets) {
  return sizeof(nstring_map_t) + buckets * sizeof(nstring_map_entry_t*);
}

// Call directly with care!!
// This expects your map to be filled with n buckets, so if you do the stupid
// thing and just declare a map at the top of your function, you will end up
// in a world of hurt and won't know why
static inline void _nstring_map_init(nstring_map_t* map,
                                     const size_t buckets,
                                     const uint32_t mask,
                                     void* (*allocator)(const size_t),
                                     void (*deallocator)(void*)) {

  (void)mask; // unused parameter

  memset(map, '\0', nstring_map_sizeof(buckets));
  map->heads = (nstring_map_entry_t**)&map[1];
  map->buckets = buckets;
  map->allocate = allocator;
  map->deallocate = deallocator;
}

/** @param mask TODO shouldn't we just calculate the mask?! */
static inline nstring_map_t* nstring_map_new(const size_t buckets,
                                             const uint32_t mask,
                                             void* (*allocator)(const size_t),
                                             void (*deallocator)(void*)) {
  nstring_map_t* map;

  if ((allocator == NULL) && (deallocator == NULL)) {
    allocator = malloc;
    deallocator = free;
  }

  if ((map = (nstring_map_t*)allocator(nstring_map_sizeof(buckets))) == NULL) {
    return map;
  }

  _nstring_map_init(map, buckets, mask, allocator, deallocator);

  return map;
}

#include "debug.h"

static inline void nstring_map_clear(nstring_map_t* map) {
  nstring_map_entry_t** head;

  for (head = &map->heads[0]; head < &map->heads[map->buckets]; head++) {
    nstring_map_entry_t* entry = *head;

    while (entry != NULL) {
      nstring_map_entry_t* next = entry->next;
      map->deallocate(entry);
      entry = next;
      map->count--;
    }
    *head = NULL;
  }
  FBI_ASSERT(map->count == 0);
}

static inline void nstring_map_del(nstring_map_t* map) {
  nstring_map_clear(map);
  map->deallocate(map);
}

static inline size_t nstring_map_size(const nstring_map_t* map) {
  return map->count;
}

static inline nstring_map_entry_t** nstring_map_prev(nstring_map_t* map,
                                                     const nstring_t* key) {
  uint32_t hash = nstring_hash(key);
  nstring_map_entry_t** nextp = &map->heads[hash % map->buckets];

  while(*nextp != NULL && nstring_cmp(&(*nextp)->key, key) != 0) {
    nextp = &(*nextp)->next;
  }
  return nextp;
}

/**
 * Set the value in a map
 * @param map map to alter
 * @param key key of item to set
 * @param value value of item to set
 * @param old_value reference to pointer in which to store old value
 *                  if old_value is NULL, it will be ignored
 * @return 0 on success, -1 on failure
 */
static inline int nstring_map_set(nstring_map_t* map,
                                  const nstring_t* key,
                                  const void* value,
                                  const void **old_value) {
  uint32_t hash = nstring_hash(key);
  nstring_map_entry_t* entry = map->heads[hash % map->buckets];
  const void *old = NULL;

  while(entry != NULL && nstring_cmp(&entry->key, key) != 0) {
    entry = entry->next;
  }

  if (entry == NULL) {
    size_t size = sizeof(nstring_map_entry_t) + key->len + 1;
    entry = (nstring_map_entry_t*)map->allocate(size);
    if (entry == NULL) {
      return -1;
    }
    entry->key.str = (char*)&entry[1];
    nstring_cpy(&entry->key, key);
    entry->value = value;
    entry->next = map->heads[hash % map->buckets];
    __sync_synchronize(); /* without this memory barrier threads executing on
                             other cores may see the fields of _entry_
                             uninitialized even after the assignment on the
                             following line inserts entry into the bucket. This
                             causes crashes. */
    map->heads[hash % map->buckets] = entry;
    map->count++;
    old = NULL;
  } else {
    old = entry->value;
    entry->value = value;
  }

  if (old_value != NULL) {
    *old_value = old;
  }

  return 0;
}

/**
 * Retrieve a value from a map
 * @param map map to alter
 * @param key key of item to get
 * @return value of item found (or NULL if not found)
 */
static inline const void* nstring_map_get(const nstring_map_t* map,
                                          const nstring_t* key) {
  uint32_t hash = nstring_hash(key);
  const nstring_map_entry_t* entry = map->heads[hash % map->buckets];

  while(entry != NULL && nstring_cmp(&entry->key, key) != 0) {
    entry = entry->next;
  }

  return entry == NULL ? NULL : entry->value;
}

/**
 * Remove a value from a map
 * @param map map to alter
 * @param key key of item to remove
 * @param old_value reference to pointer in which to store old value
 *                  if old_value is NULL, it will be ignored
 * @return void
 */
static inline void nstring_map_remove(nstring_map_t* map,
                                      const nstring_t* key,
                                      const void **old_value) {
  nstring_map_entry_t** nextp = nstring_map_prev(map, key);
  nstring_map_entry_t* deleted_entry;
  const void *old = NULL;

  if (*nextp != NULL) {
    deleted_entry = *nextp;
    *nextp = deleted_entry->next;

    old = deleted_entry->value;
    map->deallocate(deleted_entry);

    map->count--;
  }

  if (old_value != NULL) {
    *old_value = old;
  }
}

typedef struct nstring_map_iter_s {
  const nstring_map_t* map;
  size_t ix;
  nstring_map_entry_t* entry;
  nstring_map_entry_t** head;
  nstring_map_entry_t* next;
} nstring_map_iter_t;


/**
 * Initialize an iterator over _map_, setting it at the beginning of the map.
 * In multithreaded code, once an iterator has been initialized by calling this
 * function it is safe to use only as long as no entries are removed from the
 * map. This can be achieved, for instance, by holding a lock on the map while
 * iterating. Iteration over an append-oly map is generally safe, but see
 * WARNING in a comment for nstring_map_iter_has_next().
 */
static inline void nstring_map_iter_init(const nstring_map_t* map,
                                         nstring_map_iter_t *iter) {
  iter->map = map;
  iter->head = &map->heads[0];
  iter->next = NULL;
  iter->entry = NULL;
  iter->ix = 0;
}


/**
 * @return true iff _iter_ is a valid iterator on a nstring_map.
 *         See WARNING in nstring_map_iter_has_next().
 *
 */
static inline int nstring_map_iter_is_valid(const nstring_map_iter_t* iter) {
  return iter->ix <= iter->map->count;
}


/**
 * @return false if _iter_ is at the last map entry, otherwise true
 *
 * WARNING: the value returned by this function may be incorrect if it
 *          is used in multithreaded code where the underlying nstring_map
 *          can be modified from another thread. One must not expect that
 *          nstring_map_iter_next() will return a non-NULL pointer if this
 *          function returns true. That can only be assumed if the map is
 *          locked (writers are not allowed to insert items into the map or
 *          remove items from the map) before the iterator is initialized,
 *          and only for as long as the map stays locked. See
 *          https://phabricator.fb.com/D666076#comment-3 for more on the race
 *          condition. Even in single-threaded code, interleaving calls to
 *          this function with calls that modify the underlying map may
 *          make the return value not match the behavior of
 *          nstring_map_iter_next().
 */
static inline int nstring_map_iter_has_next(const nstring_map_iter_t* iter) {
  return iter->ix < iter->map->count;
}

/**
 * @return next value in map or NULL if _iter_ reached the end of the map.
 *
 * It is safe to remove the returned value from the map while
 * iterating through the map. It is generally NOT safe to interleave
 * calls to this function with calls to operations that remove
 * elements from the underlying map. In multithreaded code this
 * function is only safe to use if other threads never remove entries
 * from the map.
 */
static inline nstring_map_entry_t* nstring_map_iter_next(nstring_map_iter_t *iter) {

  nstring_map_entry_t* entry  = NULL;
  nstring_map_entry_t* next = iter->next;
  nstring_map_entry_t** head = iter->head;
  const nstring_map_t* map = iter->map;

  if (iter->ix >= map->count) {
    return NULL;
  }

  /* Scan for a non-empty bucket */

  while (next == NULL && head < &map->heads[map->buckets]) {
    next = *(head++);
  }

  /* If the current bucket has an entry, advance, and return it. */

  if (next != NULL) {
    entry = next;
    iter->ix++;
    next = next->next;
  }

  iter->entry = entry;
  iter->head = head;
  iter->next = next;

  return entry;
}


/*  nstring_map_sorted_iter functions, which allow you to
    iterate over the keys of an nstring_map in sorted order. */


typedef struct nstring_map_sorted_iter_s {
  const nstring_map_t *map;
  size_t count;
  size_t index;
  nstring_map_entry_t entry;
  nstring_t keys[0];
} *nstring_map_sorted_iter_t;


static inline int nstring_map_sorted_iter_compare(const nstring_t *a, const nstring_t *b) {
  size_t min_length = (a->len < b->len) ? a->len : b->len;
  int return_value = strncmp(a->str, b->str, min_length);
  if (return_value) {
    return return_value;
  }
  return a->len - b->len;
}

static inline nstring_map_sorted_iter_t nstring_map_sorted_iter_new(const nstring_map_t* map) {
  size_t count = nstring_map_size(map);
  nstring_map_sorted_iter_t i = (nstring_map_sorted_iter_t)
    malloc(sizeof(*i) + (count * sizeof(nstring_t)));
  if (!i) {
    return NULL;
  }

  i->map = map;
  i->count = count;
  i->index = 0;

  nstring_map_iter_t j;
  nstring_map_iter_init(map, &j);
  nstring_map_entry_t *entry;
  size_t index = 0;
  while ((entry = nstring_map_iter_next(&j)) != NULL) {
    i->keys[index++] = entry->key;
  }
  FBI_ASSERT(index == count);
  qsort(i->keys, count, sizeof(nstring_t),
        (int (*)(const void *, const void *))nstring_map_sorted_iter_compare);
  return i;
}

static inline nstring_map_entry_t* nstring_map_sorted_iter_next(nstring_map_sorted_iter_t i) {
  if (i->index >= i->count) {
    return NULL;
  }
  i->entry.key = i->keys[i->index];
  i->entry.value = nstring_map_get(i->map, &i->entry.key);
  i->index++;
  return &(i->entry);
}

static inline void nstring_map_sorted_iter_del(nstring_map_sorted_iter_t i) {
  free(i);
}

/** @return the index of needle in haystack, or -1 if not found. */
static inline ssize_t nstrstr(nstring_t haystack, nstring_t needle) {
    size_t i;
    if (haystack.len >= needle.len) {
        for (i = 0; i <= haystack.len - needle.len; ++i) {
            if (memcmp(haystack.str + i, needle.str, needle.len) == 0) {
                return i;
            }
        }
    }
    return -1;
}

/** These functions are only needed for Python. */

nstring_map_iter_t* nstring_map_iter_new(const nstring_map_t* map);

void nstring_map_iter_del(nstring_map_iter_t *iter);

nstring_t* nstring_map_iter_get_key(nstring_map_iter_t *iter);

const void* nstring_map_iter_get_value(nstring_map_iter_t *iter);

__END_DECLS

#endif
