/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Range.h>

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

namespace facebook { namespace memcache {

template <class Operation>
class McRequestWithOp;

/**
 * Class for serializing requests in ascii protocol.
 */
class AsciiSerializedRequest {
 public:
  AsciiSerializedRequest() = default;

  AsciiSerializedRequest(const AsciiSerializedRequest&) = delete;
  AsciiSerializedRequest& operator=(const AsciiSerializedRequest&) = delete;
  AsciiSerializedRequest(AsciiSerializedRequest&&) = delete;
  AsciiSerializedRequest& operator=(AsciiSerializedRequest&&) = delete;

  /**
   * Prepare buffers for given Request.
   *
   * @param request
   * @param op
   * @param iovOut  will be set to the beginning of array of ivecs that
   *                reference serialized data.
   * @param niovOut  number of valid iovecs referenced by iovOut.
   * @return true iff message was successfully prepared.
   */
  template <class Request>
  bool prepare(const Request& request, const struct iovec*& iovOut,
               size_t& niovOut);
 private:
  // We need at most 5 iovecs (lease-set):
  //   command + key + printBuffer + value + "\r\n"
  static constexpr size_t kMaxIovs = 8;
  // The longest print buffer we need is for lease-set/cas operations.
  // It requires 2 uint64, 2 uint32 + 4 spaces + "\r\n" + '\0' = 67 chars.
  static constexpr size_t kMaxBufferLength = 80;

  struct iovec iovs_[kMaxIovs];
  size_t iovsCount_{0};
  char printBuffer_[kMaxBufferLength];

  void addString(folly::ByteRange range);
  void addString(folly::StringPiece str);

  template <class Arg1, class Arg2>
  void addStrings(Arg1&& arg1, Arg2&& arg2);
  template <class Arg, class... Args>
  void addStrings(Arg&& arg, Args&&... args);

  template <class Request>
  void keyValueRequestCommon(folly::StringPiece prefix, const Request& request);

  // Get-like ops.
  void prepareImpl(const McRequestWithMcOp<mc_op_get>& request);
  void prepareImpl(const McRequestWithMcOp<mc_op_gets>& request);
  void prepareImpl(const McRequestWithMcOp<mc_op_metaget>& request);
  void prepareImpl(const McRequestWithMcOp<mc_op_lease_get>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McGetRequest>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McGetsRequest>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McMetagetRequest>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McLeaseGetRequest>& request);
  // Update-like ops.
  void prepareImpl(const McRequestWithMcOp<mc_op_set>& request);
  void prepareImpl(const McRequestWithMcOp<mc_op_add>& request);
  void prepareImpl(const McRequestWithMcOp<mc_op_replace>& request);
  void prepareImpl(const McRequestWithMcOp<mc_op_append>& request);
  void prepareImpl(const McRequestWithMcOp<mc_op_prepend>& request);
  void prepareImpl(const McRequestWithMcOp<mc_op_cas>& request);
  void prepareImpl(const McRequestWithMcOp<mc_op_lease_set>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McSetRequest>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McAddRequest>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McReplaceRequest>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McAppendRequest>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McPrependRequest>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McCasRequest>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McLeaseSetRequest>& request);
  // Arithmetic ops.
  void prepareImpl(const McRequestWithMcOp<mc_op_incr>& request);
  void prepareImpl(const McRequestWithMcOp<mc_op_decr>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McIncrRequest>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McDecrRequest>& request);
  // Delete op.
  void prepareImpl(const McRequestWithMcOp<mc_op_delete>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McDeleteRequest>& request);
  // Touch op.
  void prepareImpl(const McRequestWithMcOp<mc_op_touch>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McTouchRequest>& request);
  // Version op.
  void prepareImpl(const McRequestWithMcOp<mc_op_version>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McVersionRequest>& request);
  // FlushAll op.
  void prepareImpl(const McRequestWithMcOp<mc_op_flushall>& request);
  void prepareImpl(const TypedThriftRequest<cpp2::McFlushAllRequest>& request);

  // Everything else is false.
  template <class Request>
  std::false_type prepareImpl(const Request& request);

  struct PrepareImplWrapper;
};

}} // facebook::memcache

#include "AsciiSerialized-inl.h"
