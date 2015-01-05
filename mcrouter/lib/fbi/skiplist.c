/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
// Skip list implementation from http://epaperpress.com/sortsearch/txt/skl.txt.
// The license can be found on http://www.epaperpress.com/sortsearch/ -- "Source
// code, when part of a software project, may be used freely without reference
// to the author."

/* skip list */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "skiplist.h"

/* implementation dependent declarations */
#define compLT(a,b) (a < b)
#define compEQ(a,b) (a == b)
#define compGE(a,b) (a >= b)

/* levels range from (0 .. MAXLEVEL) */
#define MAXLEVEL 15

#define NIL list->hdr

skiplist_status_t skiplist_insert(skiplist_t* list,
                                  const uint32_t key,
                                  const void* const rec) {
  int i, newLevel;
  skiplist_node_t *update[MAXLEVEL+1];
  skiplist_node_t *x;

  /***********************************************
   *  allocate node for data and insert in list  *
   ***********************************************/

  /* find where key belongs */
  x = list->hdr;
  for (i = list->level; i >= 0; i--) {
    while (x->forward[i] != NIL
           && compLT(x->forward[i]->key, key))
      x = x->forward[i];
    update[i] = x;
  }
  x = x->forward[0];
  if (x != NIL && compEQ(x->key, key))
    return SKIPLIST_DUPLICATE_KEY;

  /* determine level */
  for (
    newLevel = 0;
    rand() < RAND_MAX/2 && newLevel < MAXLEVEL;
    newLevel++);

  if (newLevel > list->level) {
    for (i = list->level + 1; i <= newLevel; i++)
      update[i] = NIL;
    list->level = newLevel;
  }

  /* make new node */
  if ((x = skiplist_alloc(sizeof(skiplist_node_t) + newLevel*sizeof(skiplist_node_t *))) == 0)
    return SKIPLIST_MEM_EXHAUSTED;
  x->key = key;
  x->rec = rec;

  /* update forward links */
  for (i = 0; i <= newLevel; i++) {
    x->forward[i] = update[i]->forward[i];
    update[i]->forward[i] = x;
  }
  return SKIPLIST_OK;
}

skiplist_status_t skiplist_delete(skiplist_t* list, uint32_t key) {
  int i;
  skiplist_node_t *update[MAXLEVEL+1], *x;

  /*******************************************
   *  delete node containing data from list  *
   *******************************************/

  /* find where data belongs */
  x = list->hdr;
  for (i = list->level; i >= 0; i--) {
    while (x->forward[i] != NIL
           && compLT(x->forward[i]->key, key))
      x = x->forward[i];
    update[i] = x;
  }
  x = x->forward[0];
  if (x == NIL || !compEQ(x->key, key)) return SKIPLIST_KEY_NOT_FOUND;

  /* adjust forward pointers */
  for (i = 0; i <= list->level; i++) {
    if (update[i]->forward[i] != x) break;
    update[i]->forward[i] = x->forward[i];
  }

  skiplist_free (x);

  /* adjust header level */
  while ((list->level > 0)
         && (list->hdr->forward[list->level] == NIL))
    list->level--;

  return SKIPLIST_OK;
}

skiplist_status_t skiplist_find(const skiplist_t* list,
                                uint32_t key,
                                const void** rec) {
  int i;
  skiplist_node_t *x = list->hdr;

  /*******************************
   *  find node containing data  *
   *******************************/

  for (i = list->level; i >= 0; i--) {
    while (x->forward[i] != NIL
           && compLT(x->forward[i]->key, key))
      x = x->forward[i];
  }
  x = x->forward[0];
  if (x != NIL && compEQ(x->key, key)) {
    *rec = x->rec;
    return SKIPLIST_OK;
  }
  return SKIPLIST_KEY_NOT_FOUND;
}

skiplist_status_t skiplist_findnextlargest(const skiplist_t* list,
                                           const uint32_t key,
                                           const void** rec) {
  int i;
  skiplist_node_t *x = list->hdr;

  if (x->forward[0] == NIL) {
    return SKIPLIST_KEY_NOT_FOUND;
  }

  /*******************************
   *  find node containing data  *
   *******************************/

  for (i = list->level; i >= 0; i--) {
    while (x->forward[i] != NIL &&
           compLT(x->forward[i]->key, key))
      x = x->forward[i];
  }
  x = x->forward[0];
  if (x != NIL && compGE(x->key, key)) {
    *rec = x->rec;
    return SKIPLIST_OK;
  } else if (x == NIL) {
    // continuum wrap-around case.
    *rec = x->forward[0]->rec;
    return SKIPLIST_OK;
  }
  return SKIPLIST_UNKNOWN_ERROR;
}

skiplist_t* skiplist_new() {
  skiplist_t* list;
  int i;

  /**************************
   *  initialize skip list  *
   **************************/

  if ((list = skiplist_alloc(sizeof(skiplist_t))) == NULL) {
    return NULL;
  }
  if ((list->hdr = skiplist_alloc(sizeof(skiplist_node_t) +
                                  MAXLEVEL*sizeof(skiplist_node_t *))) ==
      0) {
    skiplist_free(list);
    return NULL;
  }
  for (i = 0; i <= MAXLEVEL; i++)
    list->hdr->forward[i] = NIL;
  list->level = 0;
  return list;
}

void skiplist_del(skiplist_t* list) {
  skiplist_node_t* x, * temp;

  x = list->hdr;

  do {
    temp = x->forward[0];
    skiplist_free(x);
    x = temp;
  } while (x != NIL);
  skiplist_free(list);
}

#if 0
int main(int argc, char **argv) {
  int i, maxnum, random;
  recType *rec;
  uint32_t *key;
  skiplist_status_t status;


  /* command-line:
   *
   *   skl maxnum [random]
   *
   *   skl 2000
   *       process 2000 sequential records
   *   skl 4000 r
   *       process 4000 random records
   *
   */

  maxnum = atoi(argv[1]);
  random = argc > 2;

  initList();

  if ((rec = skiplist_malloc(maxnum * sizeof(recType))) == 0) {
    fprintf (stderr, "insufficient memory (rec)\n");
    exit(1);
  }
  if ((key = skiplist_malloc(maxnum * sizeof(uint32_t))) == 0) {
    fprintf (stderr, "insufficient memory (key)\n");
    exit(1);
  }

  if (random) {
    /* fill "a" with unique random numbers */
    for (i = 0; i < maxnum; i++) key[i] = rand();
    printf ("ran, %d items\n", maxnum);
  } else {
    for (i = 0; i < maxnum; i++) key[i] = i;
    printf ("seq, %d items\n", maxnum);
  }

  for (i = 0; i < maxnum; i++) {
    status = insert(key[i], &rec[i]);
    if (status) printf("pt1: error = %d\n", status);
  }

  for (i = maxnum-1; i >= 0; i--) {
    status = find(key[i], &rec[i]);
    if (status) printf("pt2: error = %d\n", status);
  }

  for (i = maxnum-1; i >= 0; i--) {
    status = delete(key[i]);
    if (status) printf("pt3: error = %d\n", status);
  }
  return 0;
}
#endif /* #if 0 */
