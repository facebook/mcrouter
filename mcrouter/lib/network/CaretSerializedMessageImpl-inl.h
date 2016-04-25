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

namespace facebook { namespace memcache {

template <class Op>
bool CaretSerializedMessage::prepare(const McRequestWithOp<Op>& req,
                                     size_t reqId,
                                     const struct iovec*& iovOut,
                                     size_t& niovOut) noexcept {
  return prepareImpl(req, reqId, iovOut, niovOut);
}

template <int Op>
typename std::enable_if<
    !ConvertToTypedIfSupported<McRequestWithMcOp<Op>>::value,
    bool>::type
CaretSerializedMessage::prepareImpl(const McRequestWithMcOp<Op>& req,
                                    size_t reqId,
                                    const struct iovec*& iovOut,
                                    size_t& niovOut) {
  return false;
}

template <class ThriftType>
bool CaretSerializedMessage::prepare(const TypedThriftRequest<ThriftType>& req,
                                     size_t reqId,
                                     const struct iovec*& iovOut,
                                     size_t& niovOut) noexcept {
  constexpr size_t typeId =
      IdFromType<typename TypedThriftRequest<ThriftType>::rawType,
                 TRequestList>::value;

  return fill(req, reqId, typeId, req.traceId(), iovOut, niovOut);
}

template <int Op>
typename std::enable_if<
    ConvertToTypedIfSupported<McRequestWithMcOp<Op>>::value,
    bool>::type
CaretSerializedMessage::prepareImpl(const McRequestWithMcOp<Op>& req,
                                    size_t reqId,
                                    const struct iovec*& iovOut,
                                    size_t& niovOut) {
  auto treq = convertToTyped(req);

  constexpr size_t typeId =
      IdFromType<typename decltype(treq)::rawType, TRequestList>::value;

  return fill(treq, reqId, typeId, 0 /* traceId */, iovOut, niovOut);
}

template <class ThriftType>
bool CaretSerializedMessage::prepare(TypedThriftReply<ThriftType>&& reply,
                                     size_t reqId,
                                     const struct iovec*& iovOut,
                                     size_t& niovOut) noexcept {
  constexpr size_t typeId = IdFromType<ThriftType, TReplyList>::value;
  return fill(reply, reqId, typeId, 0 /* traceId */, iovOut, niovOut);
}

template <class ThriftType>
bool CaretSerializedMessage::fill(const TypedThriftMessage<ThriftType>& tmsg,
                                  uint32_t reqId,
                                  size_t typeId,
                                  uint64_t traceId,
                                  const struct iovec*& iovOut,
                                  size_t& niovOut) {
  // Serialize body into storage_. Note we must defer serialization of header.
  serializeThriftStruct(tmsg, storage_);

  UmbrellaMessageInfo info;
  info.bodySize = storage_.computeBodySize();
  info.typeId = typeId;
  info.reqId = reqId;
  info.version = UmbrellaVersion::TYPED_MESSAGE;
  info.traceId = traceId;

  fillHeader(info);

  const auto iovs = storage_.getIovecs();
  iovOut = iovs.first;
  niovOut = iovs.second;
  return true;
}

inline void CaretSerializedMessage::fillHeader(UmbrellaMessageInfo& info) {
  size_t headerSize = caretPrepareHeader(
    info, reinterpret_cast<char*>(storage_.getHeaderBuf()));
  storage_.reportHeaderSize(headerSize);
}

}} // facebook::memcache
