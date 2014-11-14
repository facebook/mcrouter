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

#include "mcrouter/lib/McReply.h"
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
        if (off + len > nbody || off + len < off) {
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

UmbrellaSerializedReply::UmbrellaSerializedReply() {
  /* These will not change from message to message */
  msg_.msg_header.magic_byte = ENTRY_LIST_MAGIC_BYTE;
  msg_.msg_header.version = UMBRELLA_VERSION_BASIC;

  iovs_[0].iov_base = &msg_;
  iovs_[0].iov_len = sizeof(msg_);

  iovs_[1].iov_base = entries_;
}

bool UmbrellaSerializedReply::prepare(const McReply& reply,
                                      mc_op_t op, uint64_t reqid,
                                      struct iovec*& iovOut, size_t& niovOut) {
  static char nul = '\0';

  /* We can reuse this struct multiple times, reset the counters */
  nEntries_ = nStrings_ = offset_ = 0;
  error_ = false;
  niovOut = 0;

  appendInt(I32, msg_op, umbrella_op_from_mc[op]);
  appendInt(U64, msg_reqid, reqid);
  appendInt(I32, msg_result, umbrella_res_from_mc[reply.result()]);

  if (reply.appSpecificErrorCode()) {
    appendInt(I32, msg_err_code, reply.appSpecificErrorCode());
  }
  if (reply.flags()) {
    appendInt(U64, msg_flags, reply.flags());
  }
  if (reply.exptime()) {
    appendInt(U64, msg_exptime, reply.exptime());
  }
  if (reply.delta()) {
    appendInt(U64, msg_delta, reply.delta());
  }
  if (reply.leaseToken()) {
    appendInt(U64, msg_lease_id, reply.leaseToken());
  }
  if (reply.cas()) {
    appendInt(U64, msg_cas, reply.cas());
  }
  if (reply.lowValue() || reply.highValue()) {
    appendDouble(reply.lowValue());
  }
  if (reply.highValue()) {
    appendDouble(reply.highValue());
  }

  /* TODO: if we intend to pass chained IOBufs as values,
     we can optimize this to write multiple iovs directly */
  if (reply.hasValue()) {
    auto valueRange = reply.valueRangeSlow();
    appendString(msg_value,
                 reinterpret_cast<const uint8_t*>(valueRange.begin()),
                 valueRange.size());
  }

  /* NOTE: this check must come after all append*() calls */
  if (error_) {
    return false;
  }

  size_t size = sizeof(entry_list_msg_t) +
    sizeof(um_elist_entry_t) * nEntries_ +
    offset_;

  msg_.total_size = folly::Endian::big((uint32_t)size);
  msg_.nentries = folly::Endian::big((uint16_t)nEntries_);

  iovs_[1].iov_len = sizeof(um_elist_entry_t) * nEntries_;
  niovOut = 2;

  for (size_t i = 0; i < nStrings_; i++) {
    iovs_[niovOut].iov_base = (char *)strings_[i].begin();
    iovs_[niovOut].iov_len = strings_[i].size();
    niovOut++;

    iovs_[niovOut].iov_base = &nul;
    iovs_[niovOut].iov_len = 1;
    niovOut++;
  }

  iovOut = iovs_;
  return true;
}

void UmbrellaSerializedReply::appendInt(
  entry_type_t type, int32_t tag, uint64_t val) {

  if (nEntries_ >= kInlineEntries) {
    error_ = true;
    return;
  }

  um_elist_entry_t& entry = entries_[nEntries_++];
  entry.type = folly::Endian::big((uint16_t)type);
  entry.tag = folly::Endian::big((uint16_t)tag);
  entry.data.val = folly::Endian::big((uint64_t)val);
}

void UmbrellaSerializedReply::appendDouble(double val) {
  if (nEntries_ >= kInlineEntries) {
    error_ = true;
    return;
  }

  um_elist_entry_t& entry = entries_[nEntries_++];
  entry.type = folly::Endian::big((uint16_t)DOUBLE);
  entry.tag = folly::Endian::big((uint16_t)msg_double);
  uint64_t doubleBits;
  static_assert(sizeof(double) == sizeof(uint64_t), "double is not 8 bytes!");
  memcpy(&doubleBits, &val, sizeof(double));
  entry.data.val = folly::Endian::big(doubleBits);
}

void UmbrellaSerializedReply::appendString(
  int32_t tag, const uint8_t* data, size_t len, entry_type_t type) {

  if (nStrings_ >= kInlineStrings) {
    error_ = true;
    return;
  }

  strings_[nStrings_++] = folly::StringPiece((const char*)data, len);

  um_elist_entry_t& entry = entries_[nEntries_++];
  entry.type = folly::Endian::big((uint16_t)type);
  entry.tag = folly::Endian::big((uint16_t)tag);
  entry.data.str.offset = folly::Endian::big((uint32_t)offset_);
  entry.data.str.len = folly::Endian::big((uint32_t)(len + 1));
  offset_ += len + 1;
}

}}
