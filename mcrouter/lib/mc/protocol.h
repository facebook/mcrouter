/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FB_MEMCACHE_MC_PROTOCOL_H
#define FB_MEMCACHE_MC_PROTOCOL_H

#include <assert.h>
#include <netinet/in.h>

#include "mcrouter/lib/fbi/decls.h"
#include "mcrouter/lib/fbi/nstring.h"
#include "mcrouter/lib/fbi/util.h"
#include "mcrouter/lib/mc/msg.h"

__BEGIN_DECLS

#define MC_KEY_MAX_LEN_ASCII (250)
#define MC_KEY_MAX_LEN_UMBRELLA (2 * 1024)
#define MC_KEY_MAX_LEN (MC_KEY_MAX_LEN_UMBRELLA)
#define MC_VALUE_MAX_LEN (1024 * 1024)


typedef enum mc_protocol_e {
  mc_unknown_protocol = 0,
  mc_ascii_protocol = 1,
  mc_binary_protocol = 2,
  mc_umbrella_protocol = 3,
  mc_nprotocols, // placeholder
} mc_protocol_t;


static inline mc_protocol_t mc_string_to_protocol(const char * str) {
  if (!strcmp(str, "ascii")) {
    return mc_ascii_protocol;
  } else if (!strcmp(str, "binary")) {
    return mc_binary_protocol;
  } else if (!strcmp(str, "umbrella")) {
    return mc_umbrella_protocol;
  } else {
    return mc_unknown_protocol;
  }
}

static inline const char* mc_protocol_to_string(const mc_protocol_t value) {
  static const char* const strings[]= {
    "unknown-protocol",
    "ascii",
    "binary",
    "umbrella",
  };
  return strings[value < mc_nprotocols ? value : mc_unknown_protocol];
}

typedef enum mc_transport_e {
  mc_unknown_transport = 0,
  mc_stream = IPPROTO_TCP,
  // We used to support IPPROTO_UDP, kind of, sort of, but that's why this
  // is an enum still...
} mc_transport_t;

static inline const char* mc_transport_to_string(const mc_transport_t value) {
  if (value == mc_stream) {
    return "tcp";
  } else {
    return "unknown";
  }
}

/** Accesspoint descriptor */
typedef struct mc_accesspoint_s {
  mc_transport_t transport;
  nstring_t host;
  nstring_t port;
  mc_protocol_t protocol;
} mc_accesspoint_t;


static inline size_t mc_accesspoint_len(const mc_accesspoint_t* accesspoint) {
  return sizeof(mc_accesspoint_t) +
    accesspoint->host.len + 1 +
    accesspoint->port.len + 1;
}


/** Copy src into dest. dest->host and dest->port need to be preallocated with
    enough memory. */
static inline void mc_accesspoint_copy(mc_accesspoint_t* dest,
                                       const mc_accesspoint_t* src) {
  dest->transport = src->transport;
  dest->protocol = src->protocol;
  nstring_copy(&dest->host, &src->host);
  nstring_copy(&dest->port, &src->port);
  return;
}

static inline int mc_accesspoint_eq(const mc_accesspoint_t* a,
                                    const mc_accesspoint_t* b) {
  FBI_ASSERT(a);
  FBI_ASSERT(b);
  return (a->transport == b->transport &&
          nstring_cmp(&a->host, &b->host) == 0 &&
          nstring_cmp(&a->port, &b->port) == 0 &&
          a->protocol == b->protocol);
}

/** Is this a reliable stream accesspoint */
static inline int mc_accesspoint_isstream(const mc_accesspoint_t* accesspoint) {
  return accesspoint->transport == mc_stream;
}

/** Is this a binary protocol accesspoint */
static inline int mc_accesspoint_isbinary(const mc_accesspoint_t* accesspoint) {
  return accesspoint->protocol == mc_binary_protocol;
}


/** Is this an ascii protocol accesspoint */
static inline int mc_accesspoint_isascii(const mc_accesspoint_t* accesspoint) {
  return accesspoint->protocol == mc_ascii_protocol;
}

/** host:port:transport:protocol */
nstring_t* mc_accesspoint_hash(const mc_accesspoint_t* accesspoint);

/** Gets the maximum number of bytes needed for an ASCII memcache
    request header. assumes max values for integer fields */
size_t mc_ascii_req_max_hdr_length(const mc_msg_t* req);

/**
 * NOTE: in case request has a value, caller must ensure that the request is
 *       alive as long as we use returned iovs.
 */
int mc_serialize_req_ascii(const mc_msg_t* req, char* headerBuffer,
    size_t headerBufferLength, struct iovec* iovs, const size_t max);

__END_DECLS

#endif /* !defined(_facebook_mcp_protocol_h_) */
