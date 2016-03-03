/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/McRequestToTypedConverter.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook {
namespace memcache {

template <class Op>
bool CaretSerializedMessage::prepare(const McRequestWithOp<Op>& req,
                                     size_t reqId,
                                     struct iovec*& iovOut,
                                     size_t& niovOut) noexcept {
  return prepareImpl(req, reqId, iovOut, niovOut);
}

template <int Op>
typename std::enable_if<
    !ConvertToTypedIfSupported<McRequestWithMcOp<Op>>::value,
    bool>::type
CaretSerializedMessage::prepareImpl(const McRequestWithMcOp<Op>& req,
                                    size_t reqId,
                                    struct iovec*& iovOut,
                                    size_t& niovOut) {
  return false;
}

template <class ThriftType>
bool CaretSerializedMessage::prepare(const TypedThriftRequest<ThriftType>& req,
                                     size_t reqId,
                                     struct iovec*& iovOut,
                                     size_t& niovOut) noexcept {
  constexpr size_t typeId =
      IdFromType<typename TypedThriftRequest<ThriftType>::rawType,
                 TRequestList>::value;

  return fill(req, reqId, typeId, iovOut, niovOut);
}

template <int Op>
typename std::enable_if<
    ConvertToTypedIfSupported<McRequestWithMcOp<Op>>::value,
    bool>::type
CaretSerializedMessage::prepareImpl(const McRequestWithMcOp<Op>& req,
                                    size_t reqId,
                                    struct iovec*& iovOut,
                                    size_t& niovOut) {
  auto treq = convertToTyped(req);

  constexpr size_t typeId =
      IdFromType<typename decltype(treq)::rawType, TRequestList>::value;

  return fill(treq, reqId, typeId, iovOut, niovOut);
}

template <class Reply>
bool CaretSerializedMessage::prepare(Reply&& reply,
                                     size_t reqId,
                                     size_t typeId,
                                     struct iovec*& iovOut,
                                     size_t& niovOut) noexcept {
  return fill(reply, reqId, typeId, iovOut, niovOut);
}

template <class ThriftType>
bool CaretSerializedMessage::fill(const TypedThriftMessage<ThriftType>& tmsg,
                                  uint32_t reqId,
                                  size_t typeId,
                                  struct iovec*& iovOut,
                                  size_t& niovOut) {
  fillBody(tmsg);

  UmbrellaMessageInfo info;
  info.bodySize = ioBuf_->computeChainDataLength();
  info.typeId = typeId;
  info.reqId = reqId;
  info.version = UmbrellaVersion::TYPED_MESSAGE;

  fillHeader(info);
  niovOut = iovsUsed_;
  iovOut = iovs_;
  return true;
}

inline void CaretSerializedMessage::fillHeader(UmbrellaMessageInfo& info) {
  size_t headerSize = caretPrepareHeader(info, headerBuf_);
  iovs_[0].iov_base = headerBuf_;
  iovs_[0].iov_len = headerSize;
  iovsUsed_++;
}

template <class ThriftType>
void CaretSerializedMessage::fillBody(
    const TypedThriftMessage<ThriftType>& tmsg) {

  ioBuf_ = serializeThriftStruct(tmsg);

  /* the first iov in the iovs_ array is reserved for the protocol header,
   * remaining kMaxIovs - 1 are used for serializing a thrift structure itself.
   */
  iovsUsed_ = ioBuf_->fillIov(iovs_ + 1, kMaxIovs - 1);
  if (iovsUsed_ == 0) {
    // IOBuf chain longer than kMaxIovs - 1
    ioBuf_->coalesce();
    iovsUsed_ = ioBuf_->fillIov(iovs_ + 1, kMaxIovs - 1);
  }
}
}
} // facebook::memcache
