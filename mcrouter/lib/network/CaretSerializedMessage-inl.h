/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/network/CarbonMessageDispatcher.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook {
namespace memcache {

template <class Request>
bool CaretSerializedMessage::prepare(
    const Request& req,
    size_t reqId,
    const CodecIdRange& supportedCodecs,
    const struct iovec*& iovOut,
    size_t& niovOut) noexcept {
  return fill(
      req,
      reqId,
      Request::typeId,
      req.traceToInts(),
      supportedCodecs,
      iovOut,
      niovOut);
}

template <class Reply>
bool CaretSerializedMessage::prepare(
    Reply&& reply,
    size_t reqId,
    const CodecIdRange& supportedCodecs,
    const CompressionCodecMap* compressionCodecMap,
    double dropProbability,
    ServerLoad serverLoad,
    const struct iovec*& iovOut,
    size_t& niovOut) noexcept {
  return fill(
      reply,
      reqId,
      Reply::typeId,
      {0, 0} /* traceId */,
      supportedCodecs,
      compressionCodecMap,
      dropProbability,
      serverLoad,
      iovOut,
      niovOut);
}

template <class Message>
bool CaretSerializedMessage::fill(
    const Message& message,
    uint32_t reqId,
    size_t typeId,
    std::pair<uint64_t, uint64_t> traceId,
    const CodecIdRange& supportedCodecs,
    const struct iovec*& iovOut,
    size_t& niovOut) {
  // Serialize body into storage_. Note we must defer serialization of header.
  try {
    serializeCarbonStruct(message, storage_);
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to serialize: " << e.what();
    return false;
  }

  UmbrellaMessageInfo info;
  if (!supportedCodecs.isEmpty()) {
    info.supportedCodecsFirstId = supportedCodecs.firstId;
    info.supportedCodecsSize = supportedCodecs.size;
  }
  fillImpl(
      info, reqId, typeId, traceId, 0.0, ServerLoad::zero(), iovOut, niovOut);
  return true;
}

template <class Message>
bool CaretSerializedMessage::fill(
    const Message& message,
    uint32_t reqId,
    size_t typeId,
    std::pair<uint64_t, uint64_t> traceId,
    const CodecIdRange& supportedCodecs,
    const CompressionCodecMap* compressionCodecMap,
    double dropProbability,
    ServerLoad serverLoad,
    const struct iovec*& iovOut,
    size_t& niovOut) {
  // Serialize and (maybe) compress body of message.
  try {
    serializeCarbonStruct(message, storage_);
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to serialize: " << e.what();
    return false;
  }

  UmbrellaMessageInfo info;

  // Maybe compress.
  auto uncompressedSize = storage_.computeBodySize();
  auto codec = (compressionCodecMap == nullptr)
      ? nullptr
      : compressionCodecMap->getBest(supportedCodecs, uncompressedSize, typeId);

  if (maybeCompress(codec, uncompressedSize)) {
    info.usedCodecId = codec->id();
    info.uncompressedBodySize = uncompressedSize;
  }

  fillImpl(
      info,
      reqId,
      typeId,
      traceId,
      dropProbability,
      serverLoad,
      iovOut,
      niovOut);
  return true;
}

inline bool CaretSerializedMessage::maybeCompress(
    CompressionCodec* codec,
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
    auto compressedBuf = codec->compress(iovs.first, iovs.second);
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

inline void CaretSerializedMessage::fillImpl(
    UmbrellaMessageInfo& info,
    uint32_t reqId,
    size_t typeId,
    std::pair<uint64_t, uint64_t> traceId,
    double dropProbability,
    ServerLoad serverLoad,
    const struct iovec*& iovOut,
    size_t& niovOut) {
  info.bodySize = storage_.computeBodySize();
  info.typeId = typeId;
  info.reqId = reqId;
  info.version = UmbrellaVersion::TYPED_MESSAGE;
  info.traceId = traceId;
  info.dropProbability =
      static_cast<uint64_t>(dropProbability * kDropProbabilityNormalizer);
  info.serverLoad = serverLoad;

  size_t headerSize = caretPrepareHeader(
      info, reinterpret_cast<char*>(storage_.getHeaderBuf()));
  storage_.reportHeaderSize(headerSize);

  const auto iovs = storage_.getIovecs();
  iovOut = iovs.first;
  niovOut = iovs.second;
}

} // memcache
} // facebook
