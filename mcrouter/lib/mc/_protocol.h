/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FB_MEMCACHE_MC_INTERNAL_PROTOCOL_H
#define FB_MEMCACHE_MC_INTERNAL_PROTOCOL_H
//#include "protocol.h"
#include "mcrouter/lib/fbi/decls.h"
#include "mcrouter/lib/mc/msg.h"

__BEGIN_DECLS

/* maximal ascii request header is:
   lease-set <key> <lease-id> <flags> <exptime> <size>\r\n */

#define MC_ASCII_LEASE_TOKEN_MAX_LEN 20 /* 18446744073709551615 */
#define MC_ASCII_FLAGS_MAX_LEN 10 /* 4294967295 */
#define MC_ASCII_EXPTIME_MAX_LEN 10 /* 4294967295 */
#define MC_ASCII_SIZE_MAX_LEN 10 /* 4294967295 */
#define MC_ASCII_REQ_HDR_MAX_LEN                \
  (strlen("lease-set") + 1 +                    \
   MC_KEY_MAX_LEN + 1 +                         \
   MC_ASCII_LEASE_TOKEN_MAX_LEN + 1 +           \
   MC_ASCII_FLAGS_MAX_LEN + 1 +                 \
   MC_ASCII_EXPTIME_MAX_LEN + 1 +               \
   MC_ASCII_SIZE_MAX_LEN + 2)
#define MC_ASCII_REQ_HDR_MAX_TOKENS 6

/* maximal ascii reply header is:
   lvalue <key> <lease-id> <flags> <size>\r\n */

#define MC_ASCII_REPLY_HDR_MAX_LEN                \
  (strlen("lvalue") + 1 +                         \
   MC_KEY_MAX_LEN + 1 +                           \
   MC_ASCII_LEASE_TOKEN_MAX_LEN + 1 +             \
   MC_ASCII_FLAGS_MAX_LEN + 1 +                   \
   MC_ASCII_SIZE_MAX_LEN + 2)
#define MC_ASCII_REPLY_HDR_MAX_TOKENS 5

ssize_t mc_ascii_req_to_hdr(const mc_msg_t* src,
                            char* buf,
                            const size_t nbuf);

/** Gets the maximum number of bytes needed for an ASCII memcache
    request header. assumes max values for integer fields */
static inline size_t mc_ascii_req_max_hdr_len(const mc_msg_t* req) {
  static size_t const fixed_lens[] = {
    [mc_op_unknown] = 0  /* unknown */,
    [mc_op_echo] = 6  /* echo\r\n */,
    [mc_op_quit] = 6  /* quit\r\n */,
    [mc_op_version] = 9  /* version\r\n */,
    [mc_op_servererr] = 0  /* servererr - place holder */,
    [mc_op_get] = 6  /* get <key>\r\n */,
    [mc_op_metaget] = 10 /* metaget <key> \r\n */,
    [mc_op_set] = 41 /* set <key> <flags> <exptime> <size>\r\n<value>\r\n */,
    [mc_op_add] = 41 /* add <key> <flags> <exptime> <size>\r\n<value>\r\n */,
    [mc_op_replace] = 45 /* replace <key> <flags> <exptime> <size>\r\n<value>\r\n */,
    [mc_op_append] = 44 /* append <key> <flags> <exptime> <size>\r\n<value>\r\n */,
    [mc_op_prepend] = 45 /* prepend <key> <flags> <exptime> <size>\r\n<value>\r\n */,
    [mc_op_cas] = 70 /* cas <key> <flags> <exptime> <bytes> <cas unique>\r\n */,
    [mc_op_delete] = 20 /* delete <key> <exptime>\r\n */,
    [mc_op_incr] = 28 /* incr <key> <value>\r\n */,
    [mc_op_decr] = 28 /* decr <key> <value>\r\n */,
    [mc_op_flushall] = 22 /* flush_all <delay>\r\n */,
    [mc_op_flushre] = 14 /* flush_regex <re>\r\n */,
    [mc_op_stats] = 8  /* stats <args>\r\n */,
    [mc_op_lease_get] = 12  /* lease-get <key>\r\n */,
    [mc_op_lease_set] = 47 /* lease-set <key> <flags> <exptime> <size>\r\n<value>\r\n */,
    [mc_op_gets] = 7 /* gets <key>\r\n */,
  };

  size_t fixed_len = fixed_lens[req->op <= mc_nops ?
                                req->op : mc_op_unknown];
  size_t variable_len;
  // if not, we are probably missing something in the table above
  FBI_ASSERT(fixed_len > 0);

  switch(req->op) {
  case mc_op_echo:
  case mc_op_quit:
  case mc_op_version:
    variable_len = 0;
    break;

  case mc_op_get:
  case mc_op_lease_get:
  case mc_op_metaget:
  case mc_op_gets:
    variable_len = req->key.len;
    break;

  case mc_op_set:
  case mc_op_add:
  case mc_op_replace:
  case mc_op_append:
  case mc_op_lease_set:
  case mc_op_cas:
    variable_len = req->key.len +
      req->value.len;
    break;

  case mc_op_delete:
  case mc_op_incr:
  case mc_op_decr:
  case mc_op_flushre:
  case mc_op_stats:
    variable_len = req->key.len;
    break;

  case mc_op_flushall:
    variable_len = 64; // 2**64 << 10**64
    break;

  default:
    FBI_ASSERT(0);
    variable_len = 0;
    break;
  }

  return fixed_len + variable_len + 1;
}

__END_DECLS

#endif
