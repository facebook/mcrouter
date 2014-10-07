/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "UmbrellaProtocol.h"

#include <folly/Bits.h>

#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/mc/umbrella.h"

#ifndef LIBMC_FBTRACE_DISABLE
#include "mcrouter/lib/mc/mc_fbtrace_info.h"
#endif

namespace facebook { namespace memcache {

McRequest umbrellaParseRequest(const folly::IOBuf& source,
                               const uint8_t* header, size_t nheader,
                               const uint8_t* body, size_t nbody,
                               mc_op_t& opOut, uint64_t& reqidOut) {
  McRequest req;
  opOut = mc_op_unknown;
  reqidOut = 0;

  auto msg = reinterpret_cast<const entry_list_msg_t*>(header);
  size_t nentries = folly::Endian::big((uint16_t)msg->nentries);
  if (reinterpret_cast<const uint8_t*>(&msg->entries[nentries])
      != header + nheader) {
    throw std::runtime_error("Invalid number of entries");
  }
  for (size_t i = 0; i < nentries; ++i) {
    auto& entry = msg->entries[i];
    size_t tag = folly::Endian::big((uint16_t)entry.tag);
    size_t val = folly::Endian::big((uint64_t)entry.data.val);
    switch (tag) {
      case msg_op:
        if (val >= UM_NOPS) {
          throw std::runtime_error("op out of range");
        }
        opOut = static_cast<mc_op_t>(umbrella_op_to_mc[val]);
        break;

      case msg_reqid:
        if (val == 0) {
          throw std::runtime_error("invalid reqid");
        }
        reqidOut = val;
        break;

      case msg_flags:
        req.setFlags(val);
        break;

      case msg_exptime:
        req.setExptime(val);
        break;

      case msg_delta:
        req.setDelta(val);
        break;

      case msg_cas:
        req.setCas(val);
        break;

      case msg_lease_id:
        req.setLeaseToken(val);
        break;

      case msg_key:
        if (!req.setKeyFrom(
              source, body +
              folly::Endian::big((uint32_t)entry.data.str.offset),
              folly::Endian::big((uint32_t)entry.data.str.len) - 1)) {
          throw std::runtime_error("Key: invalid offset/length");
        }
        break;

      case msg_value:
        if (!req.setValueFrom(
              source, body +
              folly::Endian::big((uint32_t)entry.data.str.offset),
              folly::Endian::big((uint32_t)entry.data.str.len) - 1)) {
          throw std::runtime_error("Value: invalid offset/length");
        }
        break;

#ifndef LIBMC_FBTRACE_DISABLE
      case msg_fbtrace:
      {
        auto off = folly::Endian::big((uint32_t)entry.data.str.offset);
        auto len = folly::Endian::big((uint32_t)entry.data.str.len) - 1;

        if (len > FBTRACE_METADATA_SZ) {
          throw std::runtime_error("Fbtrace metadata too large");
        }
        if (off + len > nbody) {
          throw std::runtime_error("Fbtrace metadata field invalid");
        }
        auto fbtraceInfo = new_mc_fbtrace_info(0);
        memcpy(fbtraceInfo->metadata, body + off, len);
        req.setFbtraceInfo(fbtraceInfo);
        break;
      }
#endif

      default:
        /* Ignore unknown tags silently */
        break;
    }
  }

  if (opOut == mc_op_unknown) {
    throw std::runtime_error("Request missing operation");
  }

  if (!reqidOut) {
    throw std::runtime_error("Request missing reqid");
  }

  return req;
}

}}
