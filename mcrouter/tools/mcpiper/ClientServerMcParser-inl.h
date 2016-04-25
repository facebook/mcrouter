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

namespace facebook { namespace memcache {

template <class Callback>
ClientServerMcParser<Callback>::ClientServerMcParser(Callback& callback)
    : callback_(callback) {
  initOldParser();
}

template <class Callback>
void ClientServerMcParser<Callback>::parse(folly::ByteRange data) {
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

template <class Callback>
void ClientServerMcParser<Callback>::reset() noexcept {
  // Reset old parser
  mc_parser_reset(&oldParser_);
  initOldParser();

  // Reset new parser
  parser_.reset();
}

template <class Callback>
bool ClientServerMcParser<Callback>::umMessageReady(
    const UmbrellaMessageInfo& info, const folly::IOBuf& buffer) {
  try {
    if (umbrellaIsReply(buffer.data(), info.headerSize)) {
      // Reply
      uint64_t id = umbrellaDetermineReqId(buffer.data(), info.headerSize);
      mc_op_t op = umbrellaDetermineOperation(buffer.data(), info.headerSize);
      switch (op) {
#define MC_OP(MC_OPERATION)                                                 \
        case MC_OPERATION::mc_op:                                           \
          forwardReply<McRequestWithOp<MC_OPERATION>>(                      \
              id,                                                           \
              parseReply<McRequestWithOp<MC_OPERATION>>(                    \
                  info, buffer.data(),                                      \
                  buffer.data() + info.headerSize,                          \
                  buffer));                                                 \
          break;
#include "mcrouter/lib/McOpList.h"
        default:
          forwardReply<McRequestWithMcOp<mc_op_unknown>>(
              id,
              parseReply<McRequestWithMcOp<mc_op_unknown>>(
                  info,
                  buffer.data(),
                  buffer.data() + info.headerSize,
                  buffer));
          break;
      }
    } else {
      // Request
      mc_op_t op;
      uint64_t id;
      auto req = umbrellaParseRequest(buffer,
                                      buffer.data(),
                                      info.headerSize,
                                      buffer.data() + info.headerSize,
                                      info.bodySize,
                                      op,
                                      id);
      switch (op) {
#define MC_OP(MC_OPERATION)                                                   \
        case MC_OPERATION::mc_op:                                             \
          forwardRequest(id, McRequestWithOp<MC_OPERATION>(std::move(req)));  \
          break;
#include "mcrouter/lib/McOpList.h"
        default:
          forwardRequest(id, McRequestWithMcOp<mc_op_unknown>(std::move(req)));
          break;
      }
    }
    return true;
  } catch (const std::exception& ex) {
    reset();
    return false;
  }
}

template <class Callback>
bool ClientServerMcParser<Callback>::caretMessageReady(
    const UmbrellaMessageInfo&, const folly::IOBuf&) {
  // TODO(jmswen) Add Caret support
  return false;
}

template <class Callback>
void ClientServerMcParser<Callback>::handleAscii(folly::IOBuf& readBuffer) {
  // Call old parser for ascii
  auto bytes = readBuffer.coalesce();
  mc_parser_parse(&oldParser_, bytes.begin(), bytes.size());
  readBuffer.clear();
}

template <class Callback>
void ClientServerMcParser<Callback>::parseError(mc_res_t, folly::StringPiece) {
}

template <class Callback>
void ClientServerMcParser<Callback>::initOldParser() {
  mc_parser_init(&oldParser_,
                 request_reply_parser,
                 &oldParserMsgReady,
                 &oldParserParseError,
                 this);
  oldParser_.parser_state = parser_msg_header;
  oldParser_.record_skip_key = true;
}

template <class Callback>
void ClientServerMcParser<Callback>::oldParserMsgReady(void* context,
                                                       uint64_t msgId,
                                                       mc_msg_t* msg) {
  auto parser = reinterpret_cast<ClientServerMcParser<Callback>*>(context);

  if (msg->result == mc_res_unknown) { // Request

    switch (msg->op) {
#define MC_OP(MC_OPERATION)                                                   \
      case MC_OPERATION::mc_op:                                               \
        parser->forwardRequest(                                               \
            msgId, McRequestWithOp<MC_OPERATION>(McMsgRef::moveRef(msg)));    \
        break;
#include "mcrouter/lib/McOpList.h"
      default:
        parser->forwardRequest(
            msgId, McRequestWithMcOp<mc_op_unknown>(
              McMsgRef::moveRef(msg)));
        break;
    }

  } else { // Reply

    switch (msg->op) {
#define MC_OP(MC_OPERATION)                                                   \
      case MC_OPERATION::mc_op:                                               \
        {                                                                     \
          ReplyT<McRequestWithOp<MC_OPERATION>> reply(                        \
              msg->result, McMsgRef::moveRef(msg));                           \
          parser->template forwardReply<McRequestWithOp<MC_OPERATION>>(       \
              msgId, std::move(reply));                                       \
        }                                                                     \
        break;
#include "mcrouter/lib/McOpList.h"
      default:
        {
          ReplyT<McRequestWithMcOp<mc_op_unknown>> reply(
              msg->result, McMsgRef::moveRef(msg));
          parser->template forwardReply<McRequestWithMcOp<mc_op_unknown>>(
              msgId, std::move(reply));
        }
        break;
    }

  }

  parser->oldParser_.parser_state = parser_msg_header;
}

template <class Callback>
void ClientServerMcParser<Callback>::oldParserParseError(void* context,
                                                         parser_error_t) {
  auto parser = reinterpret_cast<ClientServerMcParser*>(context);
  mc_parser_reset(&parser->oldParser_);
  parser->initOldParser();
}

template <class Callback>
template <class Request>
ReplyT<Request> ClientServerMcParser<Callback>::parseReply(
    const UmbrellaMessageInfo& info, const uint8_t* header,
    const uint8_t* body, const folly::IOBuf& bodyBuffer) {
  return umbrellaParseReply<Request>(
      bodyBuffer, header, info.headerSize, body, info.bodySize);
}

template <class Callback>
template <class Request>
void ClientServerMcParser<Callback>::forwardRequest(uint64_t id, Request req) {
  callback_.requestReady(id, std::move(req));
}

template <class Callback>
template <class Request>
void ClientServerMcParser<Callback>::forwardReply(uint64_t id,
                                                  ReplyT<Request> reply) {
  callback_.template replyReady<Request>(id, std::move(reply));
}

}} // facebook::memcache
