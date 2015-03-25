/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

namespace facebook { namespace memcache {

template <class Callback>
ClientMcParser<Callback>::ClientMcParser(Callback& cb,
                                         size_t requestsPerRead,
                                         size_t minBufferSize,
                                         size_t maxBufferSize)
  : parser_(*this, requestsPerRead, minBufferSize, maxBufferSize),
    callback_(cb) {
  mc_parser_init(&mcParser_,
                 reply_parser,
                 &parserMsgReady,
                 &parserParseError,
                 this);
}

template <class Callback>
ClientMcParser<Callback>::~ClientMcParser() {
  mc_parser_reset(&mcParser_);
}

template <class Callback>
std::pair<void*, size_t> ClientMcParser<Callback>::getReadBuffer() {
  return parser_.getReadBuffer();
}

template <class Callback>
bool ClientMcParser<Callback>::readDataAvailable(size_t len) {
  return parser_.readDataAvailable(len);
}

template <class Callback>
template <class Operation, class Request>
void ClientMcParser<Callback>::expectNext() {
  // No-op for now.
}

template <class Callback>
void ClientMcParser<Callback>::replyReadyHelper(McReply&& reply,
                                                uint64_t reqid) {
  parser_.reportMsgRead();
  callback_.replyReady(std::move(reply), reqid);
}

template <class Callback>
bool ClientMcParser<Callback>::umMessageReady(const UmbrellaMessageInfo& info,
                                              const uint8_t* header,
                                              const uint8_t* body,
                                              const folly::IOBuf& bodyBuffer) {
  auto mutMsg = createMcMsgRef();
  uint64_t reqid;
  auto st = um_consume_no_copy(header, info.headerSize, body, info.bodySize,
                               &reqid, mutMsg.get());
  if (st != um_ok) {
    callback_.parseError(mc_res_remote_error, "Error parsing Umbrella message");
    return false;
  }

  folly::IOBuf value;
  if (mutMsg->value.len != 0) {
    if (!cloneInto(value, bodyBuffer,
                   reinterpret_cast<uint8_t*>(mutMsg->value.str),
                   mutMsg->value.len)) {
      callback_.parseError(mc_res_remote_error, "Error parsing Umbrella value");
      return false;
    }
    // Reset msg->value, or it will confuse McReply::releasedMsg
    mutMsg->value.str = nullptr;
    mutMsg->value.len = 0;
  }
  McMsgRef msg(std::move(mutMsg));
  auto reply = McReply(msg->result, msg.clone());
  if (value.length() != 0) {
    reply.setValue(std::move(value));
  }
  replyReadyHelper(std::move(reply), reqid);
  return true;
}

template <class Callback>
void ClientMcParser<Callback>::handleAscii(folly::IOBuf& readBuffer) {
  /* mc_parser only works with contiguous blocks */
  auto bytes = readBuffer.coalesce();
  mc_parser_parse(&mcParser_, bytes.begin(), bytes.size());
  readBuffer.clear();
}

template <class Callback>
void ClientMcParser<Callback>::parseError(mc_res_t result,
                                          folly::StringPiece reason) {
  callback_.parseError(result, reason);
}

template <class Callback>
void ClientMcParser<Callback>::parserMsgReady(void* context,
                                              uint64_t reqid,
                                              mc_msg_t* req) {
  auto parser = reinterpret_cast<ClientMcParser<Callback>*>(context);
  auto result = req->result;
  parser->replyReadyHelper(McReply(result, McMsgRef::moveRef(req)), reqid);
}

template <class Callback>
void ClientMcParser<Callback>::parserParseError(void* context,
                                                parser_error_t error) {
  std::string err;

  switch (error) {
    case parser_unspecified_error:
    case parser_malformed_request:
      err = "malformed request";
      break;
    case parser_out_of_memory:
      err = "out of memory";
      break;
  }

  reinterpret_cast<ClientMcParser<Callback>*>(context)
    ->callback_.parseError(mc_res_client_error, err);
}

}}  // facebook::memcache
