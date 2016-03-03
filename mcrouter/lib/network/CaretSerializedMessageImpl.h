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

#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/McRequestToTypedConverter.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook {
namespace memcache {

template <class T>
class TypedThriftMessage;
template <class T>
class TypedThriftRequest;

/**
 * Class for serializing requests in the form of thrift structs.
 */
class CaretSerializedMessage {
 public:
  CaretSerializedMessage() noexcept = default;

  CaretSerializedMessage(const CaretSerializedMessage&) = delete;
  CaretSerializedMessage& operator=(const CaretSerializedMessage&) = delete;
  CaretSerializedMessage(CaretSerializedMessage&&) noexcept = delete;
  CaretSerializedMessage& operator=(CaretSerializedMessage&&) = delete;

  void clear() {
    iovsUsed_ = 0;
    ioBuf_.reset();
  }

  /**
   * Prepare requests for serialization for an Operation
   *
   * @param req      Request
   * @param iovOut   Set to the beginning of array of ivecs that
   *                 reference serialized data.
   * @param niovOut  number of valid iovecs referenced by iovOut.
   *
   * @return true iff message was successfully prepared.
   */
  template <class Op>
  bool prepare(const McRequestWithOp<Op>& req,
               size_t reqId,
               struct iovec*& iovOut,
               size_t& niovOut) noexcept;

  template <class ThriftType>
  bool prepare(const TypedThriftRequest<ThriftType>& req,
               size_t reqId,
               struct iovec*& iovOut,
               size_t& niovOut) noexcept;

  /**
   * Prepare replies for serialization
   *
   * @param  reply    TypedReply
   * @param  iovOut   will be set to the beginning of array of ivecs
   * @param  niovOut  number of valid iovecs referenced by iovOut.
   * @return true iff message was successfully prepared.
   */
  template <class Reply>
  bool prepare(Reply&& reply,
               size_t reqId,
               size_t typeId,
               struct iovec*& iovOut,
               size_t& niovOut) noexcept;

 private:
  template <class ThriftType>
  bool fill(const TypedThriftMessage<ThriftType>& tmsg,
            uint32_t reqId,
            size_t typeId,
            struct iovec*& iovOut,
            size_t& niovOut);

  void fillHeader(UmbrellaMessageInfo& info);

  template <int Op>
  typename std::enable_if<
    !ConvertToTypedIfSupported<McRequestWithMcOp<Op>>::value, bool>::type
  prepareImpl(const McRequestWithMcOp<Op>& req,
              size_t reqId,
              struct iovec*& iovOut,
              size_t& niovOut);

  template <int Op>
  typename std::enable_if<
    ConvertToTypedIfSupported<McRequestWithMcOp<Op>>::value, bool>::type
  prepareImpl(const McRequestWithMcOp<Op>& req,
              size_t reqId,
              struct iovec*& iovOut,
              size_t& niovOut);

  template <class ThriftType>
  void fillBody(const TypedThriftMessage<ThriftType>& tmsg);

  std::unique_ptr<folly::IOBuf> ioBuf_;
  static constexpr size_t kMaxIovs = 8;
  struct iovec iovs_[kMaxIovs];
  size_t iovsUsed_{0};
  char headerBuf_[kMaxHeaderLength];
};
}
} // facebook::memcache

#include "mcrouter/lib/network/CaretSerializedMessageImpl-inl.h"
