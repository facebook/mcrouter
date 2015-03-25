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

#include "mcrouter/lib/network/McParser.h"

namespace facebook { namespace memcache {

template <class Callback>
class ServerMcParser : private McParser::ParserCallback {
 public:
  ServerMcParser(Callback& cb,
                 size_t requestsPerRead,
                 size_t minBufferSize,
                 size_t maxBufferSize);

  ~ServerMcParser();

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
   * @return false  On any parse error.
   */
  bool readDataAvailable(size_t len);

  mc_protocol_t protocol() const {
    return parser_.protocol();
  }

  bool outOfOrder() const {
    return parser_.outOfOrder();
  }

 private:
  McParser parser_;
  mc_parser_t mcParser_;

  Callback& callback_;

  void requestReadyHelper(McRequest&& req, mc_op_t operation,
                          uint64_t reqid, mc_res_t result,
                          bool noreply);

  /* McParser callbacks */
  bool umMessageReady(const UmbrellaMessageInfo& info,
                      const uint8_t* header,
                      const uint8_t* body,
                      const folly::IOBuf& bodyBuffer) override;
  void handleAscii(folly::IOBuf& readBuffer) override;
  void parseError(mc_res_t result, folly::StringPiece reason) override;

  /* mc_parser_t callbacks */
  static void parserMsgReady(void* context, uint64_t reqid, mc_msg_t* req);
  static void parserParseError(void* context, parser_error_t error);
};

}}  // facebook::memcache

#include "ServerMcParser-inl.h"
