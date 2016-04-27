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

#include <type_traits>
#include <utility>

#include <folly/io/IOBuf.h>
#include <folly/Range.h>

#include "mcrouter/lib/network/CaretReplyConverter.h"
#include "mcrouter/lib/network/McAsciiParser.h"
#include "mcrouter/lib/network/McParser.h"
#include "mcrouter/lib/network/ThriftMessageTraits.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook { namespace memcache {

class McReply;

template <class Callback>
class ClientMcParser : private McParser::ParserCallback {
 public:
  ClientMcParser(Callback& cb,
                 size_t minBufferSize,
                 size_t maxBufferSize,
                 const bool useJemallocNodumpAllocator = false);

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
  void expectNext();

 private:
  McParser parser_;
  McClientAsciiParser asciiParser_;
  void (ClientMcParser<Callback>::*replyForwarder_)(){nullptr};
  void (ClientMcParser<Callback>::*umbrellaOrCaretForwarder_)(
      const UmbrellaMessageInfo&, const folly::IOBuf&, uint64_t){nullptr};
  CaretReplyConverter converter_;

  Callback& callback_;

  template <class Request>
  void forwardAsciiReply();

  template <class Request>
  void forwardUmbrellaReply(const UmbrellaMessageInfo& info,
                            const folly::IOBuf& buffer,
                            uint64_t reqId);

  template <class Request>
  typename std::enable_if<!IsCustomRequest<Request>::value, void>::type
  forwardCaretReply(const UmbrellaMessageInfo& headerInfo,
                    const folly::IOBuf& buffer,
                    uint64_t reqId);

  template <class Request>
  typename std::enable_if<IsCustomRequest<Request>::value, void>::type
  forwardCaretReply(const UmbrellaMessageInfo& headerInfo,
                    const folly::IOBuf& buffer,
                    uint64_t reqId);

  /* McParser callbacks */
  bool umMessageReady(const UmbrellaMessageInfo& info,
                      const folly::IOBuf& buffer) override final;
  bool caretMessageReady(const UmbrellaMessageInfo& headerInfo,
                         const folly::IOBuf& buffer) override final;
  void handleAscii(folly::IOBuf& readBuffer) override final;
  void parseError(mc_res_t result, folly::StringPiece reason) override final;

  bool shouldReadToAsciiBuffer() const;
};

}}  // facebook::memcache

#include "ClientMcParser-inl.h"
