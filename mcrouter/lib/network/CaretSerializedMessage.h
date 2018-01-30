/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <utility>

#include <folly/Range.h>
#include <folly/Varint.h>

#include "mcrouter/lib/Compression.h"
#include "mcrouter/lib/CompressionCodecManager.h"
#include "mcrouter/lib/carbon/CarbonQueueAppender.h"
#include "mcrouter/lib/network/ServerLoad.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook {
namespace memcache {

struct CodecIdRange;
class CompressionCodec;

/**
 * Class for serializing requests in the form of Carbon structs.
 */
class CaretSerializedMessage {
 public:
  CaretSerializedMessage() = default;

  CaretSerializedMessage(const CaretSerializedMessage&) = delete;
  CaretSerializedMessage& operator=(const CaretSerializedMessage&) = delete;
  CaretSerializedMessage(CaretSerializedMessage&&) noexcept = delete;
  CaretSerializedMessage& operator=(CaretSerializedMessage&&) = delete;

  void clear() {
    storage_.reset();
  }

  /**
   * Prepare requests for serialization for an Operation
   *
   * @param req               Request
   * @param iovOut            Set to the beginning of array of ivecs that
   *                          reference serialized data.
   * @param supportedCodecs   Range of supported compression codecs.
   * @param niovOut           Number of valid iovecs referenced by iovOut.
   *
   * @return true iff message was successfully prepared.
   */
  template <class Request>
  bool prepare(
      const Request& req,
      size_t reqId,
      const CodecIdRange& supportedCodecs,
      const struct iovec*& iovOut,
      size_t& niovOut) noexcept;

  /**
   * Prepare replies for serialization
   *
   * @param reply                 TypedReply.
   * @param reqId                 Request id.
   * @param supportedCodecs       Range of supported codecs.
   * @param compressionCodecMap   Map of available codecs.
   * @param dropProbability       Probability to drop subsequent request.
   * @param serverLoad            Represents load on the server.
   * @param iovOut                Will be set to the beginning of
   *                              array of iovecs
   * @param niovOut               Number of valid iovecs referenced by iovOut.
   *
   * @return true if message was successfully prepared.
   */
  template <class Reply>
  bool prepare(
      Reply&& reply,
      size_t reqId,
      const CodecIdRange& supportedCodecs,
      const CompressionCodecMap* compressionCodecMap,
      double dropProbability,
      ServerLoad serverLoad,
      const struct iovec*& iovOut,
      size_t& niovOut) noexcept;

 private:
  carbon::CarbonQueueAppenderStorage storage_;

  template <class Message>
  bool fill(
      const Message& message,
      uint32_t reqId,
      size_t typeId,
      std::pair<uint64_t, uint64_t> traceId,
      const CodecIdRange& supportedCodecs,
      const struct iovec*& iovOut,
      size_t& niovOut);

  template <class Message>
  bool fill(
      const Message& message,
      uint32_t reqId,
      size_t typeId,
      std::pair<uint64_t, uint64_t> traceId,
      const CodecIdRange& supportedCodecs,
      const CompressionCodecMap* compressionCodecMap,
      double dropProbability,
      ServerLoad serverLoad,
      const struct iovec*& iovOut,
      size_t& niovOut);

  void fillImpl(
      UmbrellaMessageInfo& info,
      uint32_t reqId,
      size_t typeId,
      std::pair<uint64_t, uint64_t> traceId,
      double dropProbability,
      ServerLoad serverLoad,
      const struct iovec*& iovOut,
      size_t& niovOut);

  /**
   * Compress body of message in storage_
   *
   * @param codec             Compression codec to use in compression.
   * @param uncompressedSize  Original (uncompressed) size of the body of the
   *                          message.
   * @return                  True if compression succeeds. Otherwise, false.
   */
  bool maybeCompress(CompressionCodec* codec, size_t uncompressedSize);
};

} // memcache
} // facebook

#include "mcrouter/lib/network/CaretSerializedMessage-inl.h"
