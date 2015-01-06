/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/McRequest.h"

namespace facebook { namespace memcache {

template<int Op>
bool UmbrellaSerializedMessage::prepare(const McRequest& request,
                                        McOperation<Op>, uint64_t reqid,
                                        struct iovec*& iovOut,
                                        size_t& niovOut) {
  niovOut = 0;

  appendInt(I32, msg_op, umbrella_op_from_mc[Op]);
  appendInt(U64, msg_reqid, reqid);
  if (request.flags()) {
    appendInt(U64, msg_flags, request.flags());
  }
  if (request.exptime()) {
    appendInt(U64, msg_exptime, request.exptime());
  }
  if (request.delta()) {
    appendInt(U64, msg_delta, request.delta());
  }
  if (request.leaseToken()) {
    appendInt(U64, msg_lease_id, request.leaseToken());
  }
  if (request.cas()) {
    appendInt(U64, msg_cas, request.cas());
  }

  auto key = request.fullKey();
  if (key.begin() != nullptr) {
    appendString(msg_key, reinterpret_cast<const uint8_t*>(key.begin()),
                 key.size());
  }

  auto valueRange = request.valueRangeSlow();
  if (valueRange.begin() != nullptr) {
    appendString(msg_value,
                 reinterpret_cast<const uint8_t*>(valueRange.begin()),
                 valueRange.size());
  }

#ifndef LIBMC_FBTRACE_DISABLE

  auto fbtraceInfo = request.fbtraceInfo();
  if (fbtraceInfo) {
    auto fbtraceLen = strlen(fbtraceInfo->metadata);
    appendString(msg_fbtrace,
                 reinterpret_cast<const uint8_t*>(fbtraceInfo->metadata),
                 fbtraceLen, CSTRING);
  }

#endif

  /* NOTE: this check must come after all append*() calls */
  if (error_) {
    return false;
  }

  niovOut = finalizeMessage();
  iovOut = iovs_;
  return true;
}

}} // facebook::memcache
