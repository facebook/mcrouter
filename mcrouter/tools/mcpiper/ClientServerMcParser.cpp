/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ClientServerMcParser.h"

#include <iostream>

#include <folly/Memory.h>

namespace facebook { namespace memcache {

ClientServerMcParser::ClientServerMcParser(CallbackFn callbackFn)
    : callbackFn_(callbackFn) {
  initOldParser();
}

void ClientServerMcParser::parse(const folly::ByteRange& data) {
  auto source = data.begin();
  size_t size = data.size();
  while (size > 0) {
    auto buffer = parser_.getReadBuffer();
    size_t numBytes = std::min(buffer.second, size);
    memcpy(buffer.first, source, numBytes);
    parser_.readDataAvailable(numBytes);
    size -= numBytes;
    source += numBytes;
  }
}

void ClientServerMcParser::reset() noexcept {
  // Reset old parser
  mc_parser_reset(&oldParser_);
  initOldParser();

  // Reset new parser
  parser_.reset();
}

void ClientServerMcParser::requestReady(uint64_t reqid,
                                        mc_op_t op,
                                        McRequest req) {
  callbackFn_(reqid, req.dependentMsg(op));
}

bool ClientServerMcParser::umMessageReady(const UmbrellaMessageInfo& info,
                                          const uint8_t* header,
                                          const uint8_t* body,
                                          const folly::IOBuf& bodyBuffer) {
  try {
    if (umbrellaIsReply(header, info.headerSize)) {
      // Reply
      uint64_t reqid = umbrellaDetermineReqId(header, info.headerSize);
      mc_op_t op = umbrellaDetermineOperation(header, info.headerSize);
      switch (op) {
#define MC_OP(MC_OPERATION)                                             \
        case MC_OPERATION::mc_op:                                       \
          replyReady(reqid, op,                                         \
                     parseReply<McRequestWithOp<MC_OPERATION>>(         \
                       info, header, body, bodyBuffer));                \
          break;
#include "mcrouter/lib/McOpList.h"
        default:
          replyReady(reqid, op,
                     parseReply<McRequestWithMcOp<mc_op_unknown>>(
                       info, header, body, bodyBuffer));
          break;
      }
    } else {
      // Request
      mc_op_t op;
      uint64_t reqid;
      auto req = umbrellaParseRequest(bodyBuffer,
                                      header, info.headerSize,
                                      body, info.bodySize,
                                      op, reqid);
      requestReady(reqid, op, std::move(req));
    }
    return true;
  } catch (const std::exception& ex) {
    reset();
    return false;
  }
}

void ClientServerMcParser::handleAscii(folly::IOBuf& readBuffer) {
  // Call old parser for ascii
  auto bytes = readBuffer.coalesce();
  mc_parser_parse(&oldParser_, bytes.begin(), bytes.size());
  readBuffer.clear();
}

void ClientServerMcParser::parseError(mc_res_t, folly::StringPiece) {
}

void ClientServerMcParser::initOldParser() {
  mc_parser_init(&oldParser_,
                 request_reply_parser,
                 &oldParserMsgReady,
                 &oldParserParseError,
                 this);
  oldParser_.parser_state = parser_msg_header;
  oldParser_.record_skip_key = true;
}

void ClientServerMcParser::oldParserMsgReady(void* context,
                                             uint64_t reqid,
                                             mc_msg_t* req) {
  auto parser = reinterpret_cast<ClientServerMcParser*>(context);
  parser->callbackFn_(reqid, McMsgRef::moveRef(req));
  parser->oldParser_.parser_state = parser_msg_header;
}

void ClientServerMcParser::oldParserParseError(void* context, parser_error_t) {
  auto parser = reinterpret_cast<ClientServerMcParser*>(context);
  mc_parser_reset(&parser->oldParser_);
  parser->initOldParser();
}

}} // facebook::memcache
