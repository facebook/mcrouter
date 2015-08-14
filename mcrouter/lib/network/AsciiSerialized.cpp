/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AsciiSerialized.h"

#include "mcrouter/lib/McRequest.h"

namespace facebook { namespace memcache {

void AsciiSerializedRequest::addString(folly::ByteRange range) {
  assert(iovsCount_ < kMaxIovs);
  iovs_[iovsCount_].iov_base = const_cast<unsigned char*>(range.begin());
  iovs_[iovsCount_].iov_len = range.size();
  ++iovsCount_;
}

void AsciiSerializedRequest::addString(folly::StringPiece str) {
  // cause implicit conversion.
  addString(folly::ByteRange(str));
}

template <class Request>
void AsciiSerializedRequest::keyValueRequestCommon(folly::StringPiece prefix,
                                                   const Request& request) {
  auto value = request.valueRangeSlow();
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %d %zd\r\n",
                      request.flags(), request.exptime(), value.size());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings(prefix, request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)),
             value, "\r\n");
}

// Get-like ops.

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_get>) {
  addStrings("get ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_gets>) {
  addStrings("gets ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_metaget>) {
  addStrings("metaget ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_lease_get>) {
  addStrings("lease-get ", request.fullKey(), "\r\n");
}

// Update-like ops.

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_set>) {
  keyValueRequestCommon("set ", request);
}

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_add>) {
  keyValueRequestCommon("add ", request);
}

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_replace>) {
  keyValueRequestCommon("replace ", request);
}

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_append>) {
  keyValueRequestCommon("append ", request);
}

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_prepend>) {
  keyValueRequestCommon("prepend ", request);
}

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_cas>) {
  auto value = request.valueRangeSlow();
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %d %zd %lu\r\n",
                      request.flags(), request.exptime(), value.size(),
                      request.cas());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("cas ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)), value,
             "\r\n");
}

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_lease_set>) {
  auto value = request.valueRangeSlow();
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %lu %d %zd\r\n",
                      request.leaseToken(), request.flags(), request.exptime(),
                      value.size());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("lease-set ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)), value,
             "\r\n");
}

// Arithmetic ops.

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_incr>) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu\r\n",
                      request.delta());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("incr ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_decr>) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu\r\n",
                      request.delta());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("decr ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// Delete op.

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_delete>) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %u\r\n",
                      request.exptime());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("delete ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// Version op.

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_version>) {
  addString("version\r\n");
}

// FlushAll op.

void AsciiSerializedRequest::prepareImpl(const McRequest& request,
                                         McOperation<mc_op_flushall>) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %u\r\n",
                      request.number());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("flush_all",
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

}} // facebook::memcache
