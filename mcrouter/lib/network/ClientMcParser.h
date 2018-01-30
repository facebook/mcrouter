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

#include <type_traits>
#include <utility>

#include <folly/Range.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/CompressionCodecManager.h"
#include "mcrouter/lib/debug/ConnectionFifo.h"
#include "mcrouter/lib/network/McAsciiParser.h"
#include "mcrouter/lib/network/McParser.h"
#include "mcrouter/lib/network/ReplyStatsContext.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook {
namespace memcache {

template <class Callback>
class ClientMcParser : private McParser::ParserCallback {
 public:
  ClientMcParser(
      Callback& cb,
      size_t minBufferSize,
      size_t maxBufferSize,
      const bool useJemallocNodumpAllocator = false,
      const CompressionCodecMap* compressionCodecMap = nullptr,
      ConnectionFifo* debugFifo = nullptr);

  /**
   * TAsyncTransport-style getReadBuffer().
   *
   * @return  a buffer pointer and its size that should be safe to read into.
   *
   * The caller might use less than the whole buffer, and will call
   * readDataAvailable(n) giving the actual number of bytes used from
   * the beginning of this buffer.
   */
  std::pair<void*, size_t> getReadBuffer();

  /**
   * Feeds the new data into the parser.
   *
   * @return false  On any parse error.
   */
  bool readDataAvailable(size_t len);

  /**
   * Next reply type specifier.
   *
   * This method should be called by client when it received nextAvailable(id)
   * callback. It explicitly tells parser what type of message is expected.
   */
  template <class Request>
  typename std::enable_if<ListContains<McRequestList, Request>::value>::type
  expectNext();
  template <class Request>
  typename std::enable_if<!ListContains<McRequestList, Request>::value>::type
  expectNext();

  void setProtocol(mc_protocol_t protocol) {
    parser_.setProtocol(protocol);
  }

  double getDropProbability() const;

 private:
  McParser parser_;
  McClientAsciiParser asciiParser_;
  void (ClientMcParser<Callback>::*replyForwarder_)(){nullptr};
  void (ClientMcParser<Callback>::*umbrellaOrCaretForwarder_)(
      const UmbrellaMessageInfo&,
      const folly::IOBuf&,
      uint64_t){nullptr};

  Callback& callback_;

  ConnectionFifo* debugFifo_{nullptr};

  const CompressionCodecMap* compressionCodecMap_{nullptr};

  template <class Request>
  void forwardAsciiReply();

  template <class Request>
  void forwardUmbrellaReply(
      const UmbrellaMessageInfo& info,
      const folly::IOBuf& buffer,
      uint64_t reqId);

  template <class Request>
  void forwardCaretReply(
      const UmbrellaMessageInfo& headerInfo,
      const folly::IOBuf& buffer,
      uint64_t reqId);

  std::unique_ptr<folly::IOBuf> decompress(
      const UmbrellaMessageInfo& headerInfo,
      const folly::IOBuf& buffer);

  /* McParser callbacks */
  bool umMessageReady(
      const UmbrellaMessageInfo& info,
      const folly::IOBuf& buffer) final;
  bool caretMessageReady(
      const UmbrellaMessageInfo& headerInfo,
      const folly::IOBuf& buffer) final;
  void handleAscii(folly::IOBuf& readBuffer) final;
  void parseError(mc_res_t result, folly::StringPiece reason) final;

  bool shouldReadToAsciiBuffer() const;

  ReplyStatsContext getReplyStats(const UmbrellaMessageInfo& headerInfo) const;
};
}
} // facebook::memcache

#include "ClientMcParser-inl.h"
