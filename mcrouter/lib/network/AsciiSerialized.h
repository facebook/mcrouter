/*
 *  Copyright (c) 2015, Facebook, Inc.
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

namespace facebook { namespace memcache {

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
   * Prepare buffers for given Request/Operation pair.
   *
   * @param request
   * @param op
   * @param iovOut  will be set to the beginning of array of ivecs that
   *                reference serialized data.
   * @param niovOut  number of valid iovecs referenced by iovOut.
   * @return true iff message was successfully prepared.
   */
  template <class Operation, class Request>
  bool prepare(const Request& request, Operation,
               struct iovec*& iovOut, size_t& niovOut);
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
  void prepareImpl(const McRequest& request, McOperation<mc_op_get>);
  void prepareImpl(const McRequest& request, McOperation<mc_op_gets>);
  void prepareImpl(const McRequest& request, McOperation<mc_op_metaget>);
  void prepareImpl(const McRequest& request, McOperation<mc_op_lease_get>);
  // Update-like ops.
  void prepareImpl(const McRequest& request, McOperation<mc_op_set>);
  void prepareImpl(const McRequest& request, McOperation<mc_op_add>);
  void prepareImpl(const McRequest& request, McOperation<mc_op_replace>);
  void prepareImpl(const McRequest& request, McOperation<mc_op_append>);
  void prepareImpl(const McRequest& request, McOperation<mc_op_prepend>);
  void prepareImpl(const McRequest& request, McOperation<mc_op_cas>);
  void prepareImpl(const McRequest& request, McOperation<mc_op_lease_set>);
  // Arithmetic ops.
  void prepareImpl(const McRequest& request, McOperation<mc_op_incr>);
  void prepareImpl(const McRequest& request, McOperation<mc_op_decr>);
  // Delete op.
  void prepareImpl(const McRequest& request, McOperation<mc_op_delete>);
  // Version op.
  void prepareImpl(const McRequest& request, McOperation<mc_op_version>);
  // FlushAll op.
  void prepareImpl(const McRequest& request, McOperation<mc_op_flushall>);

  // Everything else is false.
  template <class Request, class Operation>
  std::false_type prepareImpl(const Request& request, Operation);

  struct PrepareImplWrapper;
};

}} // facebook::memcache

#include "AsciiSerialized-inl.h"
