/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/network/UmbrellaProtocol.h"

#include <folly/Bits.h>

namespace facebook { namespace memcache {

template <class Callback>
ServerMcParser<Callback>::ServerMcParser(Callback& cb,
                                         size_t requestsPerRead,
                                         size_t minBufferSize,
                                         size_t maxBufferSize)
  : parser_(*this, requestsPerRead, minBufferSize, maxBufferSize),
    callback_(cb) {
  mc_parser_init(&mcParser_,
                 request_parser,
                 &parserMsgReady,
                 &parserParseError,
                 this);
}

template <class Callback>
ServerMcParser<Callback>::~ServerMcParser() {
  mc_parser_reset(&mcParser_);
}

template <class Callback>
std::pair<void*, size_t> ServerMcParser<Callback>::getReadBuffer() {
  return parser_.getReadBuffer();
}

template <class Callback>
bool ServerMcParser<Callback>::readDataAvailable(size_t len) {
  return parser_.readDataAvailable(len);
}

template <class Callback>
void ServerMcParser<Callback>::requestReadyHelper(McRequest&& req,
                                                  mc_op_t operation,
                                                  uint64_t reqid,
                                                  mc_res_t result,
                                                  bool noreply) {
  parser_.reportMsgRead();
  callback_.requestReady(std::move(req), operation, reqid, result, noreply);
}

template <class Callback>
bool ServerMcParser<Callback>::umMessageReady(const UmbrellaMessageInfo& info,
                                              const uint8_t* header,
                                              const uint8_t* body,
                                              const folly::IOBuf& bodyBuffer) {

  try {
    if (info.version == UmbrellaVersion::TYPED_MESSAGE) {
      folly::IOBuf trim;
      bodyBuffer.cloneOneInto(trim);
      trim.trimStart(body - bodyBuffer.data());
      callback_.typedRequestReady(info.typeId, trim, info.reqId);
    } else {
      mc_op_t op;
      uint64_t reqid;
      auto req = umbrellaParseRequest(bodyBuffer,
                                      header, info.headerSize,
                                      body, info.bodySize,
                                      op, reqid);
      /* Umbrella requests never include a result and are never 'noreply' */
      requestReadyHelper(std::move(req), op, reqid,
                         /* result= */ mc_res_unknown,
                         /* noreply= */ false);
    }
  } catch (const std::runtime_error& e) {
    std::string reason(
      std::string("Error parsing Umbrella message: ") + e.what());
    callback_.parseError(mc_res_remote_error, reason);
    return false;
  }
  return true;
}

template <class Callback>
void ServerMcParser<Callback>::handleAscii(folly::IOBuf& readBuffer) {
  /* mc_parser only works with contiguous blocks */
  auto bytes = readBuffer.coalesce();
  mc_parser_parse(&mcParser_, bytes.begin(), bytes.size());
  readBuffer.clear();
}

template <class Callback>
void ServerMcParser<Callback>::parseError(mc_res_t result,
                                          folly::StringPiece reason) {
  callback_.parseError(result, reason);
}

template <class Callback>
void ServerMcParser<Callback>::parserMsgReady(void* context,
                                              uint64_t reqid,
                                              mc_msg_t* req) {
  auto parser = reinterpret_cast<ServerMcParser<Callback>*>(context);

  auto operation = req->op;
  auto result = req->result;
  auto noreply = req->noreply;

  parser->requestReadyHelper(McRequest(McMsgRef::moveRef(req)), operation,
                             reqid, result, noreply);
}

template <class Callback>
void ServerMcParser<Callback>::parserParseError(void* context,
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

  reinterpret_cast<ServerMcParser<Callback>*>(context)
    ->callback_.parseError(mc_res_client_error, err);
}

}}  // facebook::memcache
