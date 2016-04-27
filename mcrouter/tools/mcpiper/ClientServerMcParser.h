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
#include "mcrouter/lib/Operation.h"

namespace folly {
class IOBuf;
} // folly

namespace facebook { namespace memcache {

constexpr size_t kReadBufferSizeMin = 256;
constexpr size_t kReadBufferSizeMax = 4096;

/**
 * A parser that can handle both client and server data.
 *
 * @param Callback  Callback containing two functions:
 *                  void requestReady(msgId, request);
 *                  void replyReady(msgId, reply);
 */
template <class Callback>
class ClientServerMcParser : private McParser::ParserCallback {
 public:
  /**
   * Creates the client/server parser.
   *
   * @param callbackFn  Callback function that will be called when a
   *                    request/reply is successfully parsed.
   */
  explicit ClientServerMcParser(Callback& callback);

  /**
   * Feed data into the parser. The callback will be called as soon
   * as a message is completely parsed.
   */
  void parse(folly::ByteRange data);

  /**
   * Resets parser
   */
  void reset() noexcept;

 private:
  McParser parser_{*this, kReadBufferSizeMin, kReadBufferSizeMax};
  mc_parser_t oldParser_;

  Callback& callback_;

  template <class Request>
  ReplyT<Request> parseReply(const UmbrellaMessageInfo& info,
                             const uint8_t* header,
                             const uint8_t* body,
                             const folly::IOBuf& bodyBuffer);

  /* Callback helpers */
  template <class Request>
  void forwardRequest(uint64_t id, Request req);
  template <class Request>
  void forwardReply(uint64_t id, ReplyT<Request> reply);

  /* McParser callbacks */
  bool umMessageReady(const UmbrellaMessageInfo& info,
                      const folly::IOBuf& buffer) override final;
  bool caretMessageReady(const UmbrellaMessageInfo& headerInfo,
                         const folly::IOBuf& buffer) override final;
  void handleAscii(folly::IOBuf& readBuffer) override final;
  void parseError(mc_res_t result, folly::StringPiece reason) override final;

  /* mc_parser_t callbacks and helpers */
  void initOldParser();
  static void oldParserMsgReady(void* context, uint64_t msgId, mc_msg_t* msg);
  static void oldParserParseError(void* context, parser_error_t error);
};

}} // facebook::memcache

#include "ClientServerMcParser-inl.h"
