/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "umbrella.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/uio.h>

#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/mc/util.h"

static inline size_t entries_array_size(int num_entries) {
  return sizeof(um_elist_entry_t) * num_entries;
}

static inline size_t entry_list_msg_size(entry_list_t *el) {
  return sizeof(entry_list_msg_t) + entries_array_size(el->nentries)
    + el->nbody + el->total_estrings_len + el->total_eiov_len;
}

static inline size_t entry_list_msg_body_size(entry_list_msg_t *msg) {
  return ntoh32(msg->total_size) - sizeof(*msg)
    - entries_array_size(ntoh16(msg->nentries));
}

/* cow_realloc -- realloc only if we allocated old_ptr.  Otherwise,
 * malloc and memcpy
 */
static inline void *cow_realloc(void *old_ptr, ssize_t old_size, int allocated,
                                ssize_t new_size) {
  if (allocated) {
    return realloc(old_ptr, new_size);
  }

  void *new_ptr = malloc(new_size);
  if (new_ptr != NULL) {
    memcpy(new_ptr, old_ptr, MIN(old_size, new_size));
  }
  return new_ptr;
}

static inline int entry_list_new_entry(entry_list_t *elist) {
  if (elist->nentries + 1 > elist->entries_size) {
    ssize_t new_size = MAX(elist->entries_size * 2, BASE_ENTRIES_SIZE);
    um_elist_entry_t *new_entries = cow_realloc(elist->entries,
                                       entries_array_size(elist->entries_size),
                                       elist->entries_allocated,
                                       entries_array_size(new_size));
    if (new_entries == NULL) {
      return -1;
    }
    elist->entries_allocated = 1;
    elist->entries_size = new_size;
    elist->entries = new_entries;
  }

  int idx = elist->nentries;
  elist->nentries++;
  return idx;
}

static inline int entry_list_append_string_like(entry_list_t *elist,
                                                entry_type_t type,
                                                int32_t tag,
                                                const void *str,
                                                int len) {
  FBI_ASSERT(type == CSTRING || type == BSTRING);
  if (elist->nbody + len > elist->body_size) {
    int new_body_size = MAX(elist->body_size * 2, elist->nbody + len);
    void *new_body = cow_realloc(elist->body, elist->body_size,
                                 elist->body_allocated, new_body_size);
    if (new_body == NULL) {
      return -1;
    }
    elist->body = new_body;
    elist->body_allocated = 1;
  }

  int entry_idx = entry_list_new_entry(elist);
  if (entry_idx < 0) {
    return -1;
  }
  um_elist_entry_t *entry = &elist->entries[entry_idx];

  entry->type = hton16(type);
  entry->tag = hton16(tag);
  entry->data.str.offset = hton32(elist->nbody);
  entry->data.str.len = hton32(len);
  memcpy(elist->body + elist->nbody, str, len);
  elist->nbody += len;

  return 0;
}

static inline int entry_list_lazy_append_string_like(entry_list_t *elist,
                                                     entry_type_t type,
                                                     int32_t tag,
                                                     const void *str,
                                                     int len) {
  FBI_ASSERT(type == BSTRING || type == CSTRING);

  // We don't want to allocate and reallocate this (and all the work
  // that goes with it), so, if we are full, just fall back
  if (elist->nestrings >= MAX_EXTERN_STRINGS) {
    return entry_list_append_string_like(elist, type, tag, str, len);
  }
  int idx = elist->nestrings++;

  int entry_idx = entry_list_new_entry(elist);
  if (entry_idx < 0) {
    elist->nestrings--;
    return -1;
  }
  um_elist_entry_t *entry = &elist->entries[entry_idx];

  entry->type = hton16(type);
  entry->tag = hton16(tag);

  elist->estrings[idx].entry_idx = entry_idx;
  elist->estrings[idx].ptr = str;
  elist->estrings[idx].len = len;
  elist->total_estrings_len += len;

  return 0;
}

static inline int entry_list_append_int_like(entry_list_t *elist, entry_type_t type,
                                      int32_t tag, uint64_t val) {
  int entry_idx = entry_list_new_entry(elist);
  if (entry_idx < 0) {
    return -1;
  }
  um_elist_entry_t *entry = &elist->entries[entry_idx];

  entry->type = hton16(type);
  entry->tag = hton16(tag);
  entry->data.val = hton64((uint64_t) val);

  return 0;
}

int entry_list_init(entry_list_t *elist) {
  FBI_ASSERT(elist != NULL);
  memset(elist, 0, sizeof(*elist));
  return 0;
}

void entry_list_cleanup(entry_list_t *elist) {
  if (elist->backing_message_allocated) {
    free(elist->backing_message);
  }
  if (elist->body_allocated) {
    free(elist->body);
  }
  if (elist->entries_allocated) {
    free(elist->entries);
  }
  memset(elist, 0, sizeof(*elist));
}

ssize_t entry_list_write_to_buf(entry_list_t *elist, char *buf, ssize_t len) {
  ssize_t required = entry_list_msg_size(elist);
  if (required <= 0 || len < required) {
    return -1;
  }

  entry_list_msg_t *msg = (entry_list_msg_t*) buf;
  msg->msg_header.magic_byte = ENTRY_LIST_MAGIC_BYTE;
  msg->msg_header.version = UMBRELLA_VERSION_BASIC;
  msg->total_size = hton32(required);
  msg->nentries = hton16(elist->nentries);

  memcpy(&msg->entries[0], elist->entries, entries_array_size(elist->nentries));

  void *body = &msg->entries[elist->nentries];
  memcpy(body, elist->body, elist->nbody);

  uint32_t i;
  uint32_t off = elist->nbody;
  for (i = 0; i < elist->nestrings; i++) {
    extern_string_t *estring = &elist->estrings[i];
    um_elist_entry_t *entry = &msg->entries[estring->entry_idx];
    entry->data.str.offset = hton32(off);
    entry->data.str.len = hton32(estring->len);
    memcpy(body + off, estring->ptr, estring->len);
    off += estring->len;
  }

  if (elist->eiov.niovs > 0) {
    extern_iov_t* eiov = &elist->eiov;
    um_elist_entry_t* entry = &elist->entries[eiov->entry_idx];
    entry->data.str.offset = hton32(off);
    entry->data.str.len = hton32(elist->total_eiov_len);
    for (i = 0; i < eiov->niovs; i++) {
      memcpy(body + off, eiov->iovs[i].iov_base, eiov->iovs[i].iov_len);
      off += eiov->iovs[i].iov_len;
    }
    memcpy(body + off, "\0", 1); /* null terminate */
  }

  return required;
}

int entry_list_emit_iovs(entry_list_t* elist, emit_iov_cb* emit_iov,
                         void* context) {
  elist->msg.msg_header.magic_byte = ENTRY_LIST_MAGIC_BYTE;
  elist->msg.msg_header.version = UMBRELLA_VERSION_BASIC;

  const uint32_t total_size = entry_list_msg_size(elist);
  elist->msg.total_size = hton32(total_size);

  elist->msg.nentries = hton16(elist->nentries);

  if (emit_iov(context, &elist->msg, sizeof(elist->msg))) {
    return -1;
  }
#ifndef NDEBUG
  size_t iov_size = sizeof(elist->msg);
#endif

  uint32_t i;
  uint32_t off = elist->nbody;
  for (i = 0; i < elist->nestrings; i++) {
    extern_string_t* estring = &elist->estrings[i];
    um_elist_entry_t* entry = &elist->entries[estring->entry_idx];
    entry->data.str.offset = hton32(off);
    entry->data.str.len = hton32(estring->len);
    off += estring->len;
  }

  if (elist->eiov.niovs > 0) {
    extern_iov_t* eiov = &elist->eiov;
    um_elist_entry_t* entry = &elist->entries[eiov->entry_idx];
    entry->data.str.offset = hton32(off);
    entry->data.str.len = hton32(elist->total_eiov_len);
  }

  const size_t entries_size = entries_array_size(elist->nentries);
  if (emit_iov(context, elist->entries, entries_size)) {
    return -1;
  }
#ifndef NDEBUG
  iov_size += entries_size;
#endif


  if (elist->nbody > 0) {
    if (emit_iov(context, elist->body, elist->nbody)) {
      return -1;
    }
#ifndef NDEBUG
    iov_size += elist->nbody;
#endif
  }

  for (i = 0; i < elist->nestrings; i++) {
    extern_string_t* estring = &elist->estrings[i];
    if (emit_iov(context, estring->ptr, estring->len)) {
      return -1;
    }
#ifndef NDEBUG
    iov_size += estring->len;
#endif
  }

  if (elist->eiov.niovs > 0) {
    extern_iov_t* eiov = &elist->eiov;
    for (i = 0; i < eiov->niovs; i++) {
      if (emit_iov(context, eiov->iovs[i].iov_base,
                   eiov->iovs[i].iov_len)) {
        return -1;
      }
#ifndef NDEBUG
      iov_size += eiov->iovs[i].iov_len;
#endif
    }

    /* write out a terminating null byte */
    if (emit_iov(context, "\0", 1)) {
      return -1;
    }
#ifndef NDEBUG
      iov_size++;
#endif
  }

  assert(iov_size == total_size);

  return 0;
}


int entry_list_to_iovecs(entry_list_t *elist, struct iovec *vecs, int max) {
  int niovs = 0;
  int req = 2 + elist->nestrings;
  if (elist->eiov.niovs > 0) {
    req += elist->eiov.niovs + 1 /* iovec for null terminator */;
  }
  if (elist->nbody > 0) {
    req++;
  }
  if (req > max) {
    return -1;
  }

  elist->msg.msg_header.magic_byte = ENTRY_LIST_MAGIC_BYTE;
  elist->msg.msg_header.version = UMBRELLA_VERSION_BASIC;
  elist->msg.total_size = hton32(entry_list_msg_size(elist));
  elist->msg.nentries = hton16(elist->nentries);

  vecs[0].iov_base = &elist->msg;
  vecs[0].iov_len = sizeof(elist->msg);

  vecs[1].iov_base = elist->entries;
  vecs[1].iov_len = entries_array_size(elist->nentries);
  niovs = 2;

  if (elist->nbody > 0) {
    vecs[2].iov_base = elist->body;
    vecs[2].iov_len = elist->nbody;
    niovs = 3;
  }

  uint32_t i;
  uint32_t off = elist->nbody;
  for (i = 0; i < elist->nestrings; i++) {
    extern_string_t *estring = &elist->estrings[i];
    um_elist_entry_t *entry = &elist->entries[estring->entry_idx];
    entry->data.str.offset = hton32(off);
    entry->data.str.len = hton32(estring->len);

    // And here's the missing memcpy, we're doing this with an iovec instead
    vecs[niovs].iov_base = (char *) estring->ptr;
    vecs[niovs].iov_len = estring->len;
    niovs++;

    off += estring->len;
  }

  if (elist->eiov.niovs > 0) {
    extern_iov_t *eiov = &elist->eiov;
    um_elist_entry_t *entry = &elist->entries[eiov->entry_idx];
    entry->data.str.offset = hton32(off);
    entry->data.str.len = hton32(elist->total_eiov_len);

    // Just copy the iovecs
    for (i = 0; i < eiov->niovs; i++) {
      vecs[niovs++] = eiov->iovs[i];
    }

    // copy the null terminator
    vecs[niovs++] = (struct iovec){.iov_base = "\0", .iov_len = 1};
  }
  return req;
}

ssize_t entry_list_read_from_buf(entry_list_t *elist, char *buf, size_t len,
                                 char* body, size_t nbody,
                                 int free_buf_when_done) {
  entry_list_msg_t *msg = (entry_list_msg_t*) buf;

  if (msg->msg_header.magic_byte != ENTRY_LIST_MAGIC_BYTE ||
      msg->msg_header.version != UMBRELLA_VERSION_BASIC) {
    dbg_error("Invalid umbrella header");
    return -1;
  }

  if (elist == NULL
      || buf == NULL
      || len < (ssize_t) sizeof(entry_list_msg_t)) {
    return -1;
  }

  const ssize_t msg_size = ntoh32(msg->total_size);
  elist->nentries = ntoh16(msg->nentries);
  ssize_t minimum_size =
    sizeof(entry_list_msg_t) + entries_array_size(elist->nentries);
  if (msg_size > len+nbody || msg_size < minimum_size) {
    return -1;
  }

  elist->backing_message_allocated = free_buf_when_done;
  elist->backing_message = buf;

  elist->entries_allocated = 0;
  elist->entries_size = ntoh16(msg->nentries);
  elist->nentries = ntoh16(msg->nentries);
  elist->entries = msg->entries;

  elist->body_allocated = 0;
  elist->body_size = entry_list_msg_body_size(msg);
  elist->nbody = entry_list_msg_body_size(msg);
  if (body != NULL) {
    if (nbody != elist->nbody) {
      return -1;
    }
    elist->body = body;
  } else {
    elist->body = &msg->entries[elist->nentries];
  }
  uint32_t i;
  uint32_t max = elist->nbody;
  uint32_t nentries = elist->nentries;
  for (i = 0; i < nentries; i++) {
    um_elist_entry_t *entry = &elist->entries[i];
    switch (ntoh16(entry->type)) {
      case CSTRING:
      case BSTRING:
      {
        uint32_t start = ntoh32(entry->data.str.offset);
        uint32_t end = start + ntoh32(entry->data.str.len);
        if (start > end || end > max) {
          goto error;
        }
        break;
      }
      case I32:
      case U32:
      case I64:
      case U64:
        break;
      default:
        goto error;
    }
  }

  return msg_size;

error:
  memset(elist, 0, sizeof(*elist));
  return -1;
}


ssize_t entry_list_preparer_read(entry_list_msg_preparer_t *prep,
                                 const char *buf,
                                 ssize_t len) {
  FBI_ASSERT(prep != NULL && buf != NULL);
  ssize_t consumed = 0;
  int32_t to_copy;

  if (len <= 0) {
    return len;
  }

  // Still getting the header
  if (prep->expected == 0) {
    FBI_ASSERT(prep->nbuf < sizeof(entry_list_msg_t));
    FBI_ASSERT(prep->msgbuf == NULL);

    // Most likely, we get the whole length in one shot
    if (prep->nbuf == 0 && len >= (ssize_t) sizeof(entry_list_msg_t)) {
      entry_list_msg_t *msg = (entry_list_msg_t*) buf;
      if (msg->msg_header.magic_byte != ENTRY_LIST_MAGIC_BYTE ||
          msg->msg_header.version != UMBRELLA_VERSION_BASIC) {
        dbg_error("Invalid umbrella header");
        return -1;
      }

      prep->expected = ntoh32(msg->total_size);

      prep->msgbuf = malloc(prep->expected);
      if (prep->msgbuf == NULL) {
        return -2;
      }
      if (sizeof(entry_list_msg_t) > prep->expected) {
        return -1;
      }
      memcpy(prep->msgbuf, msg, sizeof(entry_list_msg_t));

      buf += sizeof(entry_list_msg_t);
      len -= sizeof(entry_list_msg_t);
      prep->nbuf = sizeof(entry_list_msg_t);
      consumed += sizeof(entry_list_msg_t);
    }
    // Less likely, we already have part of the length or didn't get it all this
    // this time either
    else {
      to_copy = MIN((ssize_t) sizeof(entry_list_msg_t) - prep->nbuf, len);
      memcpy(prep->msg_header_buf + prep->nbuf, buf, to_copy);
      buf += to_copy;
      len -= to_copy;
      consumed += to_copy;
      prep->nbuf += to_copy;

      FBI_ASSERT(prep->nbuf <= sizeof(entry_list_msg_t));
      if(prep->nbuf < sizeof(entry_list_msg_t)) {
        return consumed;
      }

      entry_list_msg_t *msg = (entry_list_msg_t*) prep->msg_header_buf;
      if (msg->msg_header.magic_byte != ENTRY_LIST_MAGIC_BYTE ||
          msg->msg_header.version != UMBRELLA_VERSION_BASIC) {
        dbg_error("Invalid umbrella header");
        return -1;
      }

      prep->expected = ntoh32(msg->total_size);
      if (prep->expected == 0) {
        return -1;
      }

      prep->msgbuf = malloc(prep->expected);
      if (prep->msgbuf == NULL) {
        return -2;
      }
      if (sizeof(entry_list_msg_t) > prep->expected) {
        return -1;
      }
      memcpy(prep->msgbuf, prep->msg_header_buf, sizeof(entry_list_msg_t));
    }
  }

  // Unlikely, but if we consumed the whole thing already, let's just go home
  if (len == 0) {
    return consumed;
  }

  // So, at this point, we know how much we're expecting and we have a
  // nice buffer to put it in.
  to_copy = MIN(len, prep->expected - prep->nbuf);

  memcpy(prep->msgbuf + prep->nbuf, buf, to_copy);
  prep->nbuf += to_copy;
  consumed += to_copy;
  buf += to_copy;
  len -= to_copy;

  FBI_ASSERT(prep->nbuf <= prep->expected);
  prep->finished = (prep->nbuf == prep->expected);
  return consumed;
}


#define APPEND_INT_FUNC(etype, ctype)                                   \
  int entry_list_append_ ## etype (entry_list_t *elist,             \
                                   int32_t tag,                         \
                                   ctype x) {                           \
    return entry_list_append_int_like(elist, etype, tag, (uint64_t) x); \
  }


APPEND_INT_FUNC(I32, int32_t)
APPEND_INT_FUNC(U32, uint32_t)
APPEND_INT_FUNC(I64, int64_t)
APPEND_INT_FUNC(U64, uint64_t)

#undef APPEND_INT_FUNC


int entry_list_append_BSTRING(entry_list_t *elist,
                              int32_t tag,
                              const char *str,
                              int len) {
  return entry_list_append_string_like(elist, BSTRING, tag, str, len);
}

int entry_list_append_CSTRING(entry_list_t *elist,
                              int32_t tag,
                              const char *str) {
  int len = strlen(str) + 1;
  return entry_list_append_string_like(elist, CSTRING, tag, str, len);
}

int entry_list_lazy_append_BSTRING(entry_list_t *elist,
                                   int32_t tag,
                                   const char *str,
                                   int len) {
  return entry_list_lazy_append_string_like(elist, BSTRING, tag, str, len);
}

int entry_list_lazy_append_CSTRING(entry_list_t *elist,
                                   int32_t tag,
                                   const char *str) {
  int len = strlen(str) + 1;
  return entry_list_lazy_append_string_like(elist, CSTRING, tag, str, len);
}

int entry_list_lazy_append_IOVEC(entry_list_t* elist,
                                 int32_t tag,
                                 struct iovec* iovs,
                                 size_t niovs) {
  /* Over the wire iovecs are sent as a BSTRING that is obtained by
     concatenating all iovecs and a terminating '\0' byte */
  if (elist->eiov.niovs != 0) {
    /* only on iovec array per entry_list supported as of now */
    return -1;
  }
  int entry_idx = entry_list_new_entry(elist);
  if (entry_idx < 0) {
    return -1;
  }
  um_elist_entry_t *entry = &elist->entries[entry_idx];
  entry->type = hton16(BSTRING);
  entry->tag = hton16(tag);

  elist->eiov.iovs = iovs;
  elist->eiov.niovs = niovs;
  elist->eiov.entry_idx = entry_idx;

  elist->total_eiov_len = 0;
  uint32_t i;
  for (i = 0; i < niovs; i++) {
    elist->total_eiov_len += iovs[i].iov_len;
  }
  elist->total_eiov_len++; /* for null byte */
  return 0;
}

void print_entry_list(entry_list_t *elist) {
  uint32_t i;
  for (i = 0; i < elist->nentries; i++) {
    um_elist_entry_t *entry = &elist->entries[i];

    printf("[%d] => ", entry->tag);
    switch (entry->type) {
      case I32:
        printf("%d\n", (int32_t)entry->data.val);
        break;
      case U32:
        printf("%u\n", (uint32_t)entry->data.val);
        break;
      case I64:
        printf("%ld\n", (int64_t)entry->data.val);
        break;
      case U64:
        printf("%lu\n", (uint64_t)entry->data.val);
        break;
      case CSTRING:
        printf("\"%s\"\n", (char*)elist->body + entry->data.str.offset);
        break;
      case BSTRING:
        printf("Binary String offset = %d len = %d\n", entry->data.str.offset,
               entry->data.str.len);
        break;
      default:
        printf("Can't print this yet: %d\n", entry->type);
    }
  }
}

void entry_list_preparer_init(entry_list_msg_preparer_t *prep) {
  FBI_ASSERT(prep != NULL);
  memset(prep, 0, sizeof(*prep));
}

void entry_list_preparer_reset_after_failure(
    entry_list_msg_preparer_t *prep) {
  FBI_ASSERT(prep != NULL);
  if (prep->msgbuf != NULL) {
    free(prep->msgbuf);
    prep->msgbuf = NULL;
  }
  entry_list_preparer_init(prep);
}

ssize_t entry_list_consume_preparer(entry_list_t *elist,
                                    entry_list_msg_preparer_t *prep) {
  ssize_t x =
    entry_list_read_from_buf(elist, prep->msgbuf, prep->nbuf,
                             /*body=*/NULL, /*nbody=*/0,
                             /*free_buf_when_done=*/1);
  FBI_ASSERT(x <= 0 || x == prep->nbuf);

  if (x == prep->nbuf) {
    entry_list_preparer_init(prep);
  }

  return x;
}

int entry_list_preparer_finished(
    entry_list_msg_preparer_t *prep) {
  return prep->finished;
}
