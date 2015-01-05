/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "protocol.h"

#include <stdio.h>
#include <inttypes.h>

#include "mcrouter/lib/mc/_protocol.h"

static inline int nstring_len_for_printf(const nstring_t* ns) {
  int len = (int)ns->len;
  /* We can't printf strings with length larger than a signed int can hold.
     This should never happen anyway, so assert here. */
  FBI_ASSERT(len >= 0);
  return len;
}

#define MCASCII_KEY_REQ_TO_HDR(mr, op)                                   \
  static ssize_t mc_ascii_##mr##_req_to_hdr(const mc_msg_t* req,         \
                                            char* buf, size_t nbuf) {    \
    ssize_t size = snprintf((char*)buf, nbuf, #op" %.*s\r\n",            \
                            nstring_len_for_printf(&req->key),           \
                            req->key.str);                               \
                                                                         \
    return size >= (ssize_t)nbuf ? -size : size;                         \
  }

MCASCII_KEY_REQ_TO_HDR(get, get)
MCASCII_KEY_REQ_TO_HDR(metaget, metaget)
MCASCII_KEY_REQ_TO_HDR(lease_get, lease-get)
MCASCII_KEY_REQ_TO_HDR(gets, gets)

#define MCASCII_KEY_VALUE_REQ_TO_HDR(ur, op)                             \
  static ssize_t mc_ascii_##ur##_req_to_hdr(const mc_msg_t* req,         \
                                            char* buf, size_t nbuf) {    \
                                                                         \
    ssize_t size = snprintf((char*)buf, nbuf,                            \
                            #op" %.*s %" PRIu64 " %u %zd\r\n",           \
                            nstring_len_for_printf(&req->key),           \
                            req->key.str,                                \
                            req->flags,                                  \
                            req->exptime,                                \
                            req->value.len);                             \
                                                                         \
    return size >= (ssize_t)nbuf ? -size : size;                         \
  }

MCASCII_KEY_VALUE_REQ_TO_HDR(set, set)
MCASCII_KEY_VALUE_REQ_TO_HDR(add, add)
MCASCII_KEY_VALUE_REQ_TO_HDR(replace, replace)
MCASCII_KEY_VALUE_REQ_TO_HDR(append, append)

static ssize_t mc_ascii_cas_req_to_hdr(const mc_msg_t* req,
                                       char* buf, size_t nbuf) {

  ssize_t size = snprintf((char*)buf, nbuf,
                          "cas %.*s %" PRIu64 " %u %zd %" PRIu64 "\r\n",
                          nstring_len_for_printf(&req->key),
                          req->key.str,
                          req->flags,
                          req->exptime,
                          req->value.len,
                          req->cas);

  return size >= (ssize_t)nbuf ? -size : size;
}

#define MCASCII_KEY_LVALUE_REQ_TO_HDR(ur, op)                            \
  static ssize_t mc_ascii_##ur##_req_to_hdr(const mc_msg_t* req,         \
                                            char* buf, size_t nbuf) {    \
                                                                         \
    ssize_t size = snprintf((char*)buf, nbuf,                            \
                            #op" %.*s %"PRIu64" %"PRIu64" %u %zd\r\n",   \
                            nstring_len_for_printf(&req->key),           \
                            req->key.str,                                \
                            req->lease_id,                               \
                            req->flags,                                  \
                            req->exptime,                                \
                            req->value.len);                             \
                                                                         \
    return size >= (ssize_t)nbuf ? -size : size;                         \
  }

MCASCII_KEY_LVALUE_REQ_TO_HDR(lease_set, lease-set)

#define MCASCII_KEY_NUM_REQ_TO_HDR(knr, op)                              \
  static ssize_t mc_ascii_##knr##_req_to_hdr(const mc_msg_t* req,        \
                                             char* buf, size_t nbuf) {   \
    ssize_t size = snprintf((char*)buf, nbuf, #op" %.*s %"PRIu64"\r\n",  \
                            nstring_len_for_printf(&req->key),           \
                            req->key.str,                                \
                            req->delta);                                 \
    return size >= (ssize_t)nbuf ? -size : size;                         \
  }

MCASCII_KEY_NUM_REQ_TO_HDR(incr, incr)
MCASCII_KEY_NUM_REQ_TO_HDR(decr, decr)

static ssize_t mc_ascii_delete_req_to_hdr(const mc_msg_t* req,
                                          char* buf, size_t nbuf) {
  ssize_t size = snprintf((char*)buf, nbuf, "delete %.*s %u\r\n",
                          nstring_len_for_printf(&req->key),
                          req->key.str,
                          req->exptime);
  return size >= (ssize_t)nbuf ? -size : size;
}

#define MCASCII_NUM_REQ_TO_HDR(nr, op)                                   \
  static ssize_t mc_ascii_##nr##_req_to_hdr(const mc_msg_t* req,         \
                                            char* buf, size_t nbuf) {    \
    ssize_t size = snprintf((char*)buf, nbuf, #op" %u\r\n",              \
                            req->number);                                \
    return size >= (ssize_t)nbuf ? -size : size;                         \
  }

MCASCII_NUM_REQ_TO_HDR(flushall, flush_all)

#define MC_ASCII_STRING_REQ_TO_HDR(sr)                                   \
  static ssize_t mc_ascii_##sr##_req_to_hdr(const mc_msg_t* req,         \
                                            char* buf, size_t nbuf) {    \
                                                                         \
    ssize_t size = snprintf((char*)buf, nbuf, #sr"%s%.*s\r\n",           \
                            (req->key.len > 0) ? " " : "",               \
                            nstring_len_for_printf(&req->key),           \
                            req->key.str ?: "");                         \
    return size >= (ssize_t)nbuf ? -size : size;                         \
  }

MC_ASCII_STRING_REQ_TO_HDR(stats)
MC_ASCII_STRING_REQ_TO_HDR(flushre)

#define MC_ASCII_EMPTY_REQ_TO_HDR(er)                                    \
  static ssize_t mc_ascii_##er##_req_to_hdr(const mc_msg_t* req,         \
                                            char* buf, size_t nbuf) { \
    ssize_t size = snprintf((char*)buf, nbuf, #er"\r\n");                \
    return size >= (ssize_t)nbuf ? -size : size;                         \
  }

MC_ASCII_EMPTY_REQ_TO_HDR(version)
MC_ASCII_EMPTY_REQ_TO_HDR(echo)
MC_ASCII_EMPTY_REQ_TO_HDR(quit)

ssize_t mc_ascii_req_to_hdr(const mc_msg_t* req,
                            char* buf, size_t nbuf) {
  static ssize_t (*const funcs[mc_nops])(const mc_msg_t*, char*, size_t) = {
    /* unknown */ NULL,
    [mc_op_echo] = mc_ascii_echo_req_to_hdr,
    [mc_op_quit] = mc_ascii_quit_req_to_hdr,
    [mc_op_version] = mc_ascii_version_req_to_hdr,
    [mc_op_servererr] = NULL,
    [mc_op_get] = mc_ascii_get_req_to_hdr,
    [mc_op_metaget] = mc_ascii_metaget_req_to_hdr,
    [mc_op_set] = mc_ascii_set_req_to_hdr,
    [mc_op_add] = mc_ascii_add_req_to_hdr,
    [mc_op_replace] = mc_ascii_replace_req_to_hdr,
    [mc_op_append] = mc_ascii_append_req_to_hdr,
    [mc_op_prepend] = NULL /* mc_ascii_prepend_req_to_hdr */,
    [mc_op_cas] = mc_ascii_cas_req_to_hdr,
    [mc_op_delete] = mc_ascii_delete_req_to_hdr,
    [mc_op_incr] = mc_ascii_incr_req_to_hdr,
    [mc_op_decr] = mc_ascii_decr_req_to_hdr,
    [mc_op_flushall] = mc_ascii_flushall_req_to_hdr,
    [mc_op_flushre] = mc_ascii_flushre_req_to_hdr,
    [mc_op_stats] = mc_ascii_stats_req_to_hdr,
    [mc_op_verbosity] = NULL /* mc_ascii_verbosity_req_to_hdr */,
    [mc_op_lease_get] = mc_ascii_lease_get_req_to_hdr,
    [mc_op_lease_set] = mc_ascii_lease_set_req_to_hdr,
    [mc_op_gets] = mc_ascii_gets_req_to_hdr,
  };

  ssize_t (*func)(const mc_msg_t*, char*, size_t);

  func = funcs[req->op < mc_nops ? req->op : mc_op_unknown];
  if(func != NULL) {
    return func(req, buf, nbuf);
  }
  return -1;
}

/* host:port:transport:protocol */
nstring_t* mc_accesspoint_hash(const mc_accesspoint_t* accesspoint) {
  FBI_ASSERT(accesspoint->host.len <= INET6_ADDRSTRLEN);
  FBI_ASSERT(accesspoint->port.len <= strlen("65535"));
  size_t len = INET6_ADDRSTRLEN + strlen(":65535:TCP:fbbinary");
  char str[len + 1];

  char *s = str;
  size_t n = sizeof(str);

  FBI_ASSERT(n > accesspoint->host.len + 1);
  strncpy(s, accesspoint->host.str, n);
  n -= accesspoint->host.len + 1;
  s += accesspoint->host.len;
  *s++ = ':';

  FBI_ASSERT(n > accesspoint->port.len + 1);
  strncpy(s, accesspoint->port.str, n);
  n -= accesspoint->port.len + 1;
  s += accesspoint->port.len;
  *s++ = ':';

  FBI_ASSERT(n > 3);
  strncpy(s, (accesspoint->transport == mc_stream) ? "TCP" : "UDP", n);
  n -= 4;
  s += 3;
  *s++ = ':';

  FBI_ASSERT(accesspoint->protocol != mc_unknown_protocol);
  const char *protocol = mc_protocol_to_string(accesspoint->protocol);
  size_t plen = strlen(protocol);
  FBI_ASSERT(n > plen);
  strncpy(s, protocol, n);
  n -= plen + 1;
  s += plen;
  *s = 0;

  FBI_ASSERT(strlen(str) == (size_t)(s - str));
  return nstring_new(str, s - str);
}

size_t mc_ascii_req_max_hdr_length(const mc_msg_t* req) {
  return mc_ascii_req_max_hdr_len(req);
}

int mc_serialize_req_ascii(const mc_msg_t* req, char* headerBuffer,
    size_t headerBufferLength, struct iovec* iovs, const size_t max) {
  if (max < 1) {
    return -1;
  }

  ssize_t len = mc_ascii_req_to_hdr(req, headerBuffer, headerBufferLength);
  if (len < 0) {
    return len;
  }

  int niovs = 0;

  iovs[niovs].iov_base = headerBuffer;
  iovs[niovs].iov_len = len;
  ++niovs;

  if (mc_req_has_value(req)) {
    if (max < 3) {
      return -1;
    }
    iovs[niovs].iov_base = req->value.str;
    iovs[niovs].iov_len = req->value.len;
    niovs++;
    iovs[niovs].iov_base = "\r\n";
    iovs[niovs].iov_len = 2;
    niovs++;
  }

  return niovs;
}
