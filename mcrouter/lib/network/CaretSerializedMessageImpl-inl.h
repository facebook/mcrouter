/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/Compression.h"
#include "mcrouter/lib/CompressionCodecManager.h"
#include "mcrouter/lib/network/ThriftMsgDispatcher.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook { namespace memcache {

template <class ThriftType>
bool CaretSerializedMessage::prepare(const TypedThriftRequest<ThriftType>& req,
                                     size_t reqId,
                                     const CodecIdRange& supportedCodecs,
                                     const struct iovec*& iovOut,
                                     size_t& niovOut) noexcept {
  constexpr size_t typeId =
      IdFromType<typename TypedThriftRequest<ThriftType>::rawType,
                 TRequestList>::value;

  return fill(req,
              reqId,
              typeId,
              req.traceId(),
              supportedCodecs,
              iovOut,
              niovOut);
}

template <class ThriftType>
bool CaretSerializedMessage::prepare(TypedThriftReply<ThriftType>&& reply,
                                     size_t reqId,
                                     CompressionCodec* codec,
                                     const struct iovec*& iovOut,
                                     size_t& niovOut) noexcept {
  constexpr size_t typeId = IdFromType<ThriftType, TReplyList>::value;
  return fill(reply, reqId, typeId, 0 /* traceId */, codec, iovOut, niovOut);
}

template <class ThriftType>
bool CaretSerializedMessage::fill(const TypedThriftRequest<ThriftType>& tmsg,
                                  uint32_t reqId,
                                  size_t typeId,
                                  uint64_t traceId,
                                  const CodecIdRange& supportedCodecs,
                                  const struct iovec*& iovOut,
                                  size_t& niovOut) {
  // Serialize body into storage_. Note we must defer serialization of header.
  serializeThriftStruct(tmsg, storage_);

  UmbrellaMessageInfo info;
  info.supportedCodecsFirstId = supportedCodecs.firstId;
  info.supportedCodecsSize = supportedCodecs.size;
  fillImpl(info, reqId, typeId, traceId, iovOut, niovOut);
  return true;
}

template <class ThriftType>
bool CaretSerializedMessage::fill(const TypedThriftReply<ThriftType>& tmsg,
                                  uint32_t reqId,
                                  size_t typeId,
                                  uint64_t traceId,
                                  CompressionCodec* codec,
                                  const struct iovec*& iovOut,
                                  size_t& niovOut) {

  // Serialize and (maybe) compress body of message.
  serializeThriftStruct(tmsg, storage_);

  UmbrellaMessageInfo info;

  // Maybe compress.
  auto uncompressedSize = storage_.computeBodySize();
  if (maybeCompress(codec, uncompressedSize)) {
    info.usedCodecId = codec->id();
    info.uncompressedBodySize = uncompressedSize;
  }

  fillImpl(info, reqId, typeId, traceId, iovOut, niovOut);
  return true;
}

inline bool CaretSerializedMessage::maybeCompress(CompressionCodec* codec,
                                                  size_t uncompressedSize) {
  if (!codec) {
    return false;
  }

  if (UNLIKELY(uncompressedSize > std::numeric_limits<uint32_t>::max()) ||
      uncompressedSize < codec->filteringOptions().minCompressionThreshold) {
    return false;
  }

  static constexpr size_t kCompressionOverhead = 4;
  try {
    const auto iovs = storage_.getIovecs();
    // The first iovec is the header - we need to compress just the data.
    auto compressedBuf = codec->compress(iovs.first + 1, iovs.second - 1);
    auto compressedSize = compressedBuf->computeChainDataLength();
    if ((compressedSize + kCompressionOverhead) < uncompressedSize) {
      storage_.reset();
      storage_.append(*compressedBuf);
      return true;
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Error compressing reply: " << e.what();
  }

  return false;
}

inline void CaretSerializedMessage::fillImpl(UmbrellaMessageInfo& info,
                                             uint32_t reqId,
                                             size_t typeId,
                                             uint64_t traceId,
                                             const struct iovec*& iovOut,
                                             size_t& niovOut) {
  info.bodySize = storage_.computeBodySize();
  info.typeId = typeId;
  info.reqId = reqId;
  info.version = UmbrellaVersion::TYPED_MESSAGE;
  info.traceId = traceId;

  size_t headerSize = caretPrepareHeader(
    info, reinterpret_cast<char*>(storage_.getHeaderBuf()));
  storage_.reportHeaderSize(headerSize);

  const auto iovs = storage_.getIovecs();
  iovOut = iovs.first;
  niovOut = iovs.second;
}

}} // facebook::memcache
