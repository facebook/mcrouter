/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/lib/mc/msg.h"

namespace folly {
class IOBuf;
}

namespace facebook { namespace memcache {

class McRequest;

/**
 * Parse an on-the-wire Umbrella request.
 *
 * @param source           Unchained IOBuf; [body, body + nbody) must point
 *                         inside it.
 * @param header, nheader  [header, header + nheader) must point to a valid
 *                         Umbrella header.
 * @param body, nbody      [body, body + nbody) must point to a valid
 *                         Umbrella body stored in the `source` IOBuf
 * @paramOut opOut         Parsed operation.
 * @paramOut reqidOut      Parsed request ID
 * @return                 Parsed request.
 * @throws                 std::runtime_error on any parse error.
 */
McRequest umbrellaParseRequest(const folly::IOBuf& source,
                               const uint8_t* header, size_t nheader,
                               const uint8_t* body, size_t nbody,
                               mc_op_t& opOut, uint64_t& reqidOut);

}}
