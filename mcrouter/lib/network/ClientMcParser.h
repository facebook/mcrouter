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

#include "mcrouter/lib/network/CaretReplyConverter.h"
#include "mcrouter/lib/network/McAsciiParser.h"
#include "mcrouter/lib/network/McParser.h"

namespace facebook { namespace memcache {

template <class Callback>
class ClientMcParser : private McParser::ParserCallback {
 public:
  ClientMcParser(Callback& cb,
                 size_t requestsPerRead,
                 size_t minBufferSize,
                 size_t maxBufferSize,
                 mc_protocol_t protocol);

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
  template <class Operation, class Request>
  void expectNext();
 private:
  McParser parser_;
  McAsciiParser asciiParser_;
  void (ClientMcParser<Callback>::*replyForwarder_)(){nullptr};
  void (ClientMcParser<Callback>::*umbrellaForwarder_)(
    const UmbrellaMessageInfo&, const uint8_t*, const uint8_t*,
    const folly::IOBuf&, uint64_t){nullptr};
  CaretReplyConverter converter_;

  mc_protocol_t protocol_;

  Callback& callback_;

  void replyReadyHelper(McReply&& reply, uint64_t reqid);

  template <class Operation, class Request>
  void forwardAsciiReply();

  template <class Operation, class Request>
  void forwardUmbrellaReply(const UmbrellaMessageInfo& info,
                            const uint8_t* header,
                            const uint8_t* body,
                            const folly::IOBuf& bodyBuffer,
                            uint64_t reqId);

  /* McParser callbacks */
  bool umMessageReady(const UmbrellaMessageInfo& info,
                      const uint8_t* header,
                      const uint8_t* body,
                      const folly::IOBuf& bodyBuffer) override;
  void handleAscii(folly::IOBuf& readBuffer) override;
  void parseError(mc_res_t result, folly::StringPiece reason) override;

  bool shouldReadToAsciiBuffer() const;
};

}}  // facebook::memcache

#include "ClientMcParser-inl.h"
