/*
 *  Copyright (c) 2016, Facebook, Inc.
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

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_get>& request) {
  addStrings("get ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_gets>& request) {
  addStrings("gets ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_metaget>& request) {
  addStrings("metaget ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_lease_get>& request) {
  addStrings("lease-get ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McGetRequest>& request) {
  addStrings("get ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McGetsRequest>& request) {
  addStrings("gets ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McMetagetRequest>& request) {
  addStrings("metaget ", request.fullKey(), "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McLeaseGetRequest>& request) {
  addStrings("lease-get ", request.fullKey(), "\r\n");
}

// Update-like ops.

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_set>& request) {
  keyValueRequestCommon("set ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_add>& request) {
  keyValueRequestCommon("add ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_replace>& request) {
  keyValueRequestCommon("replace ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_append>& request) {
  keyValueRequestCommon("append ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_prepend>& request) {
  keyValueRequestCommon("prepend ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_cas>& request) {
  auto value = request.valueRangeSlow();
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %d %zd %lu\r\n",
                      request.flags(), request.exptime(), value.size(),
                      request.cas());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("cas ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)), value,
             "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_lease_set>& request) {
  auto value = request.valueRangeSlow();
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %lu %d %zd\r\n",
                      request.leaseToken(), request.flags(), request.exptime(),
                      value.size());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("lease-set ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)), value,
             "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McSetRequest>& request) {
  keyValueRequestCommon("set ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McAddRequest>& request) {
  keyValueRequestCommon("add ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McReplaceRequest>& request) {
  keyValueRequestCommon("replace ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McAppendRequest>& request) {
  keyValueRequestCommon("append ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McPrependRequest>& request) {
  keyValueRequestCommon("prepend ", request);
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McCasRequest>& request) {
  auto value = request.valueRangeSlow();
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %d %zd %lu\r\n",
                      request.flags(), request.exptime(), value.size(),
                      request->get_casToken());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("cas ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)), value,
             "\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McLeaseSetRequest>& request) {
  auto value = request.valueRangeSlow();
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu %lu %d %zd\r\n",
                      request->get_leaseToken(), request.flags(),
                      request.exptime(), value.size());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("lease-set ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)), value,
             "\r\n");
}

// Arithmetic ops.

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_incr>& request) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu\r\n",
                      request.delta());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("incr ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_decr>& request) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu\r\n",
                      request.delta());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("decr ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McIncrRequest>& request) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu\r\n",
                      request->get_delta());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("incr ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McDecrRequest>& request) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %lu\r\n",
                      request->get_delta());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("decr ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// Delete op.

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_delete>& request) {
  addStrings("delete ", request.fullKey());
  if (request.exptime() != 0) {
    auto len = snprintf(printBuffer_, kMaxBufferLength, " %d\r\n",
                        request.exptime());
    assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
    addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
  } else {
    addString("\r\n");
  }
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McDeleteRequest>& request) {
  addStrings("delete ", request.fullKey());
  if (request.exptime() != 0) {
    auto len = snprintf(printBuffer_, kMaxBufferLength, " %d\r\n",
                        request.exptime());
    assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
    addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
  } else {
    addString("\r\n");
  }
}

// Touch op.

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_touch>& request) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %u\r\n",
                      request.exptime());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("touch ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McTouchRequest>& request) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %u\r\n",
                      request.exptime());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("touch ", request.fullKey(),
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// Version op.

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_version>& request) {
  addString("version\r\n");
}

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McVersionRequest>& request) {
  addString("version\r\n");
}

// FlushAll op.

void AsciiSerializedRequest::prepareImpl(
    const McRequestWithMcOp<mc_op_flushall>& request) {
  auto len = snprintf(printBuffer_, kMaxBufferLength, " %u\r\n",
                      request.number());
  assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
  addStrings("flush_all",
             folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
}

// FlushAll op.

void AsciiSerializedRequest::prepareImpl(
    const TypedThriftRequest<cpp2::McFlushAllRequest>& request) {
  addString("flush_all");
  if (const auto* delay = request->get_delay()) {
    auto len = snprintf(printBuffer_, kMaxBufferLength, " %u",
                        *delay);
    assert(len > 0 && static_cast<size_t>(len) < kMaxBufferLength);
    addString(folly::StringPiece(printBuffer_, static_cast<size_t>(len)));
  }
  addString("\r\n");
}

}} // facebook::memcache
