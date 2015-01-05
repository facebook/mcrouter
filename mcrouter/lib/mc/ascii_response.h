/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FB_MEMCACHE_ASCII_RESPONSE_H
#define FB_MEMCACHE_ASCII_RESPONSE_H

#include "mcrouter/lib/fbi/decls.h"

//#include "mcrouter/lib/mc/generic.h"
#include "mcrouter/lib/mc/msg.h"

__BEGIN_DECLS

#define SCRATCH_BUFFER_LEN 96

/**
 * Temporary storage for Ascii protocol responses.
 * Must be initialized with mc_ascii_response_buf_init() before use and
 * cleaned up with mc_ascii_response_buf_cleanup() after the iovs are not
 * needed anymore.
 */
typedef struct mc_ascii_response_buf_s {
  char buffer[SCRATCH_BUFFER_LEN];
  size_t offset;
  char* stats;
} mc_ascii_response_buf_t;

void mc_ascii_response_buf_init(mc_ascii_response_buf_t* buf);
void mc_ascii_response_buf_cleanup(mc_ascii_response_buf_t* buf);

/**
 * For given request and reply, builds the Ascii protocol response.
 *
 * @param buf    Storage to use for response substrings
 * @param key    Original request key
 * @param op     Original operation
 * @param reply  Reply
 * @param iovs   Pointer to an IOV array of max_iovs size
 *
 * @return The number of IOVs used, or 0 if an error occurred.
 */
size_t mc_ascii_response_write_iovs(mc_ascii_response_buf_t* buf,
                                    nstring_t key,
                                    mc_op_t op,
                                    const mc_msg_t* reply,
                                    struct iovec* iovs,
                                    size_t max_iovs);

__END_DECLS

#endif
