/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "msg.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "mcrouter/lib/fbi/nstring.h"
#include "mcrouter/lib/mc/protocol.h"

#ifndef LIBMC_FBTRACE_DISABLE
#include "mcrouter/lib/mc/mc_fbtrace_info.h"
#endif

// For compress/uncompress
// For the uninitiated: nzlib format is just
//    magic : decompressed_size : zlib format
// It allows us to decompress in one pass later (because we know how
// big it'll end up being) and let's us double check that nothing too
// weird has happened with the value (thanks to the magic word)
#define NZLIB_MAGIC 0x6e7a6c69 /* nzli */
typedef struct nzlib_format_s {
  uint32_t magic;
  uint32_t decompressed_sz;
  uint8_t buf[0];
} nzlib_format_t;


/* Return 1 if pointer p lies within mc_msg_t msg's body
 *        0 if pointer p to p + len does not overlap at all
 * assert that p <-> p + len does not partially overlap
 */
static inline int _in_msg(const mc_msg_t *msg, void *p, size_t n) {
  FBI_ASSERT(msg);
  void *end = (void*)msg + sizeof(mc_msg_t) + msg->_extra_size;
  if (p >= (void*)msg && p < end) {
    FBI_ASSERT(p + n <= end);
    return 1;
  }
  FBI_ASSERT(p + n <= (void*)msg || p > end); // No partial overlap
  return 0;
}

static int _mc_msg_use_atomic_refcounts = 1;
void mc_msg_use_atomic_refcounts(int enable){
  _mc_msg_use_atomic_refcounts = enable;
}

#ifndef FBCODE_OPT_BUILD
#define DEFAULT_TRACK_NUM_OUTSTANDING 1
#else
#define DEFAULT_TRACK_NUM_OUTSTANDING 0
#endif
static int _mc_msg_track_num_outstanding = DEFAULT_TRACK_NUM_OUTSTANDING;
void mc_msg_track_num_outstanding(int enable) {
  _mc_msg_track_num_outstanding = enable;
}

static uint64_t _mc_msg_num_outstanding = 0;
uint64_t mc_msg_num_outstanding() {
  return _mc_msg_num_outstanding;
}

void mc_msg_init_not_refcounted(mc_msg_t* msg) {
  FBI_ASSERT(msg);
  memset(msg, 0, sizeof(*msg));
  msg->_refcount = MSG_NOT_REFCOUNTED;
}

mc_msg_t* mc_msg_new(size_t extra_size) {
  mc_msg_t* msg = malloc(sizeof(mc_msg_t) + extra_size);
  if (msg == NULL) {
    return NULL;
  }

  memset(msg, 0, sizeof(*msg));
  mc_msg_incref(msg);
  msg->_extra_size = extra_size;

  if (_mc_msg_track_num_outstanding) {
    __sync_fetch_and_add(&_mc_msg_num_outstanding, 1);
  }

  return msg;
}

// Create a new message with a copy of the key (uses mc_msg_new).
mc_msg_t *mc_msg_new_with_key(const char *key) {
  return mc_msg_new_with_key_full(key, strlen(key));
}

mc_msg_t *mc_msg_new_with_key_full(const char *key, size_t nkey) {
  mc_msg_t *msg = mc_msg_new(nkey + 1);  // + 1 for '\0'.
  char *ptr = (char*)msg;
  ptr += sizeof(mc_msg_t);
  memcpy(ptr, key, nkey);
  msg->key.str = ptr;
  msg->key.str[nkey] = '\0';
  msg->key.len = nkey;
  return msg;
}

// Create a new message with a copy of the key (uses mc_msg_new).
mc_msg_t *mc_msg_new_with_key_and_value(const char *key,
                                        const char *value,
                                        size_t nvalue) {
  return mc_msg_new_with_key_and_value_full(key, strlen(key),
                                            value, nvalue);
}

mc_msg_t *mc_msg_new_with_key_and_value_full(const char *key,
                                             size_t nkey,
                                             const char *value,
                                             size_t nvalue) {
  // add + 2 for '\0' for both key & value
  mc_msg_t *msg = mc_msg_new(nkey + nvalue + 2);
  char *ptr = (char*)msg;

  ptr += sizeof(mc_msg_t);

  // copy the key and set the pointers
  memcpy(ptr, key, nkey);
  ptr[nkey] = '\0';

  msg->key.str = ptr;
  msg->key.len = nkey;

  // move past the key
  ptr += nkey + 1;

  // copy the value and set the pointers
  memcpy(ptr, value, nvalue);
  ptr[nvalue] = '\0';

  msg->value.str = ptr;
  msg->value.len = nvalue;
  return msg;
}

int mc_msg_contains(const mc_msg_t *msg, void *p, size_t n) {
  return _in_msg(msg, p, n);
}

/* copy src's contents into dst
 * dst->_extra_size must be greater than or equal to src->_extra_size
 * If any string fields occur within src's whole message, they will be
 * deep copied into dst's deep message.  External strings will be shallow
 * copied
 */
static void _msgcpy(mc_msg_t *dst, const mc_msg_t *src) {
  FBI_ASSERT(dst && (dst->_refcount > 0 || dst->_refcount == MSG_NOT_REFCOUNTED));
  FBI_ASSERT(src && (src->_refcount > 0 || src->_refcount == MSG_NOT_REFCOUNTED));
  FBI_ASSERT(dst->_extra_size >= src->_extra_size);

  // The difference between the old msg and the new message
  // By adding this to void*'s we can shift the pointer forward pretty easily
  //
  // Example:
  //  ________(delta)_________
  // |                        |
  // v                        v
  // |   src  ...  |          |   dst  ...   |
  //            ^                        ^
  //            ptr                      ptr
  //            |________(delta)_________|
  //
  // dst->ptr = (void*)src->ptr + delta
  ssize_t delta = (void*)dst - (void*)src;

  int _refcount = dst->_refcount;
  size_t _extra_size = dst->_extra_size;
  memcpy(dst, src, sizeof(mc_msg_t) + src->_extra_size);
  dst->_refcount = _refcount; // restore
  dst->_extra_size = _extra_size; // restore
#ifndef LIBMC_FBTRACE_DISABLE
  if (src->fbtrace_info) {
    dst->fbtrace_info = mc_fbtrace_info_deep_copy(src->fbtrace_info);
  }
#endif

  if (src->stats != NULL) {
    int i, stats_count = src->number * 2;
    FBI_ASSERT(stats_count > 0);
    if (_in_msg(src, src->stats, sizeof(src->stats[0]) * stats_count)) {
      dst->stats = (void*)(src->stats) + delta;
      for (i = 0; i < stats_count; i++) {
        if (_in_msg(src, src->stats[i].str, src->stats[i].len)) {
          dst->stats[i].str = (void*)(src->stats[i].str) + delta;
        }
      }
    }
  }
  if (_in_msg(src, src->key.str, src->key.len)) {
    dst->key.str = (void*)(src->key.str) + delta;
  }
  if (_in_msg(src, src->value.str, src->value.len)) {
    dst->value.str = (void*)(src->value.str) + delta;
  }
}

/* duplicate a mc_msg_t
 * for deep/shallow copying semantics, see _msgcpy
 */
void mc_msg_copy(mc_msg_t *dst, const mc_msg_t *src) {
  _msgcpy(dst, src);
}

void mc_msg_shallow_copy(mc_msg_t *dst, const mc_msg_t *src) {
  FBI_ASSERT(dst && src);
  int _refcount = dst->_refcount;
  size_t _extra_size = dst->_extra_size;
  *dst = *src;
  dst->_refcount = _refcount;
  dst->_extra_size = _extra_size;
#ifndef LIBMC_FBTRACE_DISABLE
  if (src->fbtrace_info) {
    dst->fbtrace_info = mc_fbtrace_info_deep_copy(src->fbtrace_info);
  }
#endif
}

mc_msg_t* mc_msg_dup(const mc_msg_t *msg) {
  FBI_ASSERT(msg);
  FBI_ASSERT(msg->_refcount > 0 || msg->_refcount == MSG_NOT_REFCOUNTED);
  mc_msg_t *msg_copy = mc_msg_new(msg->_extra_size);
  if (msg_copy == NULL) {
    return NULL;
  }
  _msgcpy(msg_copy, msg);
  return msg_copy;
}

mc_msg_t* mc_msg_dup_append_key_full(const mc_msg_t *msg,
                                     const char* key_append,
                                     size_t nkey_append) {
  FBI_ASSERT(msg);
  FBI_ASSERT(msg->_refcount > 0 || msg->_refcount == MSG_NOT_REFCOUNTED);
  FBI_ASSERT(key_append);

  // Stats are not supported.
  if (msg->stats) {
    return NULL;
  }

  if (!nkey_append) {
    return mc_msg_dup(msg);
  }

  size_t new_extra_size = msg->_extra_size + nkey_append;
  if (!_in_msg(msg, msg->key.str, msg->key.len)) {
    new_extra_size += msg->key.len + 1; // +1 for null terminator
  }
  mc_msg_t* const msg_copy = mc_msg_new(new_extra_size);
  if (msg_copy == NULL) {
    return NULL;
  }
  mc_msg_shallow_copy(msg_copy, msg);

  // The new message's key is always embedded.
  msg_copy->key.len = msg->key.len + nkey_append;
  msg_copy->key.str = (void*) msg_copy + sizeof(*msg_copy);
  memcpy(msg_copy->key.str, msg->key.str, msg->key.len);
  memcpy(msg_copy->key.str + msg->key.len, key_append, nkey_append);
  msg_copy->key.str[msg_copy->key.len] = 0;

  if (_in_msg(msg, msg->value.str, msg->value.len)) {
    // The value starts after the key, including the null terminator.
    msg_copy->value.str = msg_copy->key.str + msg_copy->key.len + 1;
    memcpy(msg_copy->value.str, msg->value.str, msg_copy->value.len);
  }
  return msg_copy;
}

/* reallocate a mc_msg_t
 * for deep/shallow copying semantics, see _msgcpy
 * For more on why we do it this way, see Task #689247 and D314525
 */
mc_msg_t* mc_msg_realloc(mc_msg_t *msg, size_t new_extra_size) {
  if (msg == NULL) {
    // Same behavior as realloc, malloc on NULL
    return mc_msg_new(new_extra_size);
  }

  if (new_extra_size <= msg->_extra_size) {
    return msg;
  }

  mc_msg_t *msg_copy = mc_msg_new(new_extra_size);
  if (msg_copy == NULL) {
    // Same behavior as realloc, don't clean up msg
    return NULL;
  }

  _msgcpy(msg_copy, msg);
  msg_copy->_extra_size = new_extra_size;

  // "free" it
  mc_msg_decref(msg);

  return msg_copy;
}

int mc_msg_grow(mc_msg_t **msg_ptr, size_t len, void **field_ptr) {
  FBI_ASSERT(msg_ptr != NULL && field_ptr != NULL);
  mc_msg_t *msg = *msg_ptr;

  // assert that the field_ptr refers to something within the *msg
  FBI_ASSERT(_in_msg(msg, field_ptr, sizeof(*field_ptr)));

  size_t field_ptr_offset = (void*)field_ptr - (void*)msg;
  size_t field_offset = sizeof(*msg) + msg->_extra_size;

  mc_msg_t *new_msg = mc_msg_realloc(msg, msg->_extra_size + len);
  if (new_msg == NULL) {
    return -1;
  }

  msg = new_msg;

  void **new_field_ptr = (void**) ((void*)msg + field_ptr_offset);


  FBI_ASSERT((void*)field_ptr-(void*)(*msg_ptr) == (void*)new_field_ptr-(void*)msg);

  *new_field_ptr = ((void*)msg) + field_offset;
  *msg_ptr = msg;

  return 0;
}

// atomically increment the refcount using __sync_fetch_and_add
mc_msg_t* mc_msg_incref(mc_msg_t* msg) {
  FBI_ASSERT(msg != NULL);
  if (msg->_refcount != MSG_NOT_REFCOUNTED) {
    FBI_ASSERT(msg->_refcount >= 0);
    if (_mc_msg_use_atomic_refcounts) {
      __sync_fetch_and_add(&msg->_refcount, 1);
    } else {
#ifndef FBCODE_OPT_BUILD
      FBI_ASSERT(__sync_bool_compare_and_swap(&msg->_refcount, msg->_refcount,
                                              msg->_refcount + 1) == 1);
#else
      msg->_refcount++;
#endif
    }
  }
  return msg;
}

// atomically decrement the refcount using __sync_add_and_fetch
// to get the value after the decrement. If the value drops
// to zero free the msg
void mc_msg_decref(mc_msg_t* msg) {
  FBI_ASSERT(msg != NULL);
  if (msg->_refcount != MSG_NOT_REFCOUNTED) {
    FBI_ASSERT(msg->_refcount > 0);

    int new_refcount;
    if (_mc_msg_use_atomic_refcounts) {
      new_refcount = __sync_add_and_fetch(&msg->_refcount, -1);
    } else {
#ifndef FBCODE_OPT_BUILD
      FBI_ASSERT(__sync_bool_compare_and_swap(&msg->_refcount, msg->_refcount,
                                              msg->_refcount - 1) == 1);
      new_refcount = msg->_refcount;
#else
      new_refcount = --msg->_refcount;
#endif
    }

    if (new_refcount == 0) {
      if (_mc_msg_track_num_outstanding) {
        __sync_fetch_and_add(&_mc_msg_num_outstanding, -1);
      }
#ifndef LIBMC_FBTRACE_DISABLE
      mc_fbtrace_info_decref(msg->fbtrace_info);
#endif
#ifndef FBCODE_OPT_BUILD
      memset(msg, 'P', sizeof(*msg));
#endif
      free(msg);
    }
  }
}

/**
 * Best effort at compressing the value within msg.  Shouldn't be a
 * big deal if this fails.  This will overwrite the value buffer if
 * it's stored in the messages exta space and the compressed size is
 * smaller (otherwise it will leave it uncompressed)
 */
void mc_msg_compress(mc_msg_t **msgP) {
  // use this by default
  mc_msg_nzlib_compress(msgP);
}

int mc_msg_decompress(mc_msg_t **msgP) {
  FBI_ASSERT(msgP && *msgP);
  mc_msg_t *msg = *msgP;

  if (msg->value.str == NULL
      || msg->value.len == 0) {
    return 0;
  }

  if (msg->flags & MC_MSG_FLAG_NZLIB_COMPRESSED) {
    return mc_msg_nzlib_decompress(msgP);
  }

  // Not compressed
  return 0;
}

void mc_msg_nzlib_compress(mc_msg_t **msgP) {
  FBI_ASSERT(msgP && *msgP);
  mc_msg_t *msg = *msgP;
  nzlib_format_t *format = NULL;
  size_t len = 0;
  int rc;

  if (msg->value.str == NULL
      || msg->value.len <= 0
      || (msg->flags & MC_MSG_FLAG_NZLIB_COMPRESSED)) {
    goto epilogue;
  }


  // Get the upper bound on the deflated value length
  len = compressBound(msg->value.len);
  format = malloc(sizeof(*format) + len);
  if (format == NULL) {
    goto epilogue;
  }
  format->magic = htonl(NZLIB_MAGIC);
  format->decompressed_sz = htonl(msg->value.len);

  rc = compress(format->buf, &len, (uint8_t*)msg->value.str, msg->value.len);
  if (rc != Z_OK) {
    goto epilogue;
  }

  len += sizeof(*format);
  if (len >= msg->value.len) {
    goto epilogue;
  }

  // If msg->value is not in the msg, grow the msg so we don't trample
  // someone else's value buffer
  if (!_in_msg(msg, msg->value.str, msg->value.len)
      && mc_msg_grow(&msg, len + 1, (void**) &msg->value.str) != 0) {
    goto epilogue;
  }

  msg->value.len = len;
  memcpy(msg->value.str, format, len);
  msg->value.str[len] = '\0';
  msg->flags |= MC_MSG_FLAG_NZLIB_COMPRESSED;

  *msgP = msg;

epilogue:
  if (format) {
    free(format);
  }
}

int mc_msg_nzlib_decompress(mc_msg_t **msgP) {
  mc_msg_t *msg = *msgP;
  nzlib_format_t *format;
  uint8_t *buf = NULL;
  size_t len = 0;
  int rc = -1;

  if (msg->value.len < sizeof(*format)) {
    rc = -1;
    goto epilogue;
  }

  // This could result in unaligned reads, which will crash on some
  // platforms
  format = (nzlib_format_t*) msg->value.str;
  len = ntohl(format->decompressed_sz);
  if (ntohl(format->magic) != NZLIB_MAGIC || len == 0) {
    rc = -1;
    goto epilogue;
  }

  buf = malloc(len + 1);
  if (buf == NULL) {
    rc = -1;
    goto epilogue;
  }

  rc = uncompress(buf, &len, format->buf, msg->value.len - sizeof(*format));
  if (rc != Z_OK) {
    rc = -1;
    goto epilogue;
  }
  buf[len] = '\0';

  // Unfortunately, we're just going to waste the space of the
  // compressed value contents within the message for now.  This
  // is perfectly safe, but ugly.  If the value is not within the msg
  // struct's extra space, then we're not actually wasting anything.
  if (mc_msg_grow(&msg, len, (void**)&msg->value.str) != 0) {
    rc = -1;
    goto epilogue;
  }

  msg->value.len = len;
  memcpy(msg->value.str, buf, len);
  msg->flags &= ~MC_MSG_FLAG_NZLIB_COMPRESSED;

  *msgP = msg;

  rc = 0;

epilogue:
  if (buf) {
    free(buf);
  }
  return rc;
}

mc_op_t mc_op_from_string(const char* str) {
  int i = 0;
  for (i = mc_op_unknown; i < mc_nops; ++i) {
    if (0 == strcmp(mc_op_to_string((mc_op_t)i), str)) {
      return (mc_op_t)i;
    }
  }
  return mc_op_unknown;
}

mc_req_err_t mc_client_req_check(const mc_msg_t* req) {
  if (!mc_req_has_key(req)) {
    return mc_req_err_valid;
  }

  return mc_client_req_key_check(req->key);
}

mc_req_err_t mc_client_req_key_check(nstring_t key) {
  size_t i;

  if (key.len < 1) {
    return mc_req_err_no_key;
  }

  if (key.len > MC_KEY_MAX_LEN) {
    return mc_req_err_key_too_long;
  }

  for (i = 0; i < key.len; ++i) {
    if (iscntrl(key.str[i]) || isspace(key.str[i])) {
      return mc_req_err_space_or_ctrl;
    }
  }

  return mc_req_err_valid;
}

const char* mc_req_err_to_string(const mc_req_err_t err) {
  switch (err) {
    case mc_req_err_valid: return "valid";
    case mc_req_err_no_key: return "invalid key: missing";
    case mc_req_err_key_too_long: return "invalid key: too long";
    case mc_req_err_space_or_ctrl:
      return "invalid key: space or control character";
  }
  return "unknown";
}
