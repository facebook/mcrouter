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

#include <functional>

#include <folly/Range.h>

#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/McParser.h"

namespace folly {
class IOBuf;
}

namespace facebook { namespace memcache {

constexpr size_t kReadBufferSizeMin = 256;
constexpr size_t kReadBufferSizeMax = 4096;

/**
 * A parser that can handle both client and server data.
 * Useful for sniffers.
 */
class ClientServerMcParser : private McParser::ParserCallback {
 public:
  using CallbackFn = std::function<void(uint64_t, McMsgRef)>;

  /**
   * Creates the client/server parser.
   *
   * @param callbackFn  Callback function that will be called when a
   *                    request/reply is successfully parsed.
   */
  explicit ClientServerMcParser(CallbackFn callbackFn);

  /**
   * Feed data into the parser. The callback will be called as soon
   * as a message is completely parsed.
   */
  void parse(const folly::ByteRange& data);

  /**
   * Resets parser
   */
  void reset() noexcept;

 private:
  McParser parser_{*this, 0, kReadBufferSizeMin, kReadBufferSizeMax};
  mc_parser_t oldParser_;

  CallbackFn callbackFn_;

  template <class Request>
  ReplyT<Request> parseReply(const UmbrellaMessageInfo& info,
                             const uint8_t* header,
                             const uint8_t* body,
                             const folly::IOBuf& bodyBuffer);

  /* Callback helpers */
  void requestReady(uint64_t reqid, mc_op_t op, McRequest req);
  template <class Reply>
  void replyReady(uint64_t reqid, mc_op_t op, Reply reply);

  /* McParser callbacks */
  bool umMessageReady(const UmbrellaMessageInfo& info,
                      const uint8_t* header,
                      const uint8_t* body,
                      const folly::IOBuf& bodyBuffer) override;
  void handleAscii(folly::IOBuf& readBuffer) override;
  void parseError(mc_res_t result, folly::StringPiece reason) override;

  /* mc_parser_t callbacks and helpers */
  void initOldParser();
  static void oldParserMsgReady(void* context, uint64_t reqid, mc_msg_t* req);
  static void oldParserParseError(void* context, parser_error_t error);
};

}} // facebook::memcache

#include "ClientServerMcParser-inl.h"
