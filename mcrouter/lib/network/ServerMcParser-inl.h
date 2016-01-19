/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Bits.h>

#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook { namespace memcache {

template <class Callback>
ServerMcParser<Callback>::ServerMcParser(Callback& cb,
                                         size_t requestsPerRead,
                                         size_t minBufferSize,
                                         size_t maxBufferSize)
  : parser_(*this, requestsPerRead, minBufferSize, maxBufferSize),
    asciiParser_(*this),
    callback_(cb) {
}

template <class Callback>
ServerMcParser<Callback>::~ServerMcParser() {
}

template <class Callback>
std::pair<void*, size_t> ServerMcParser<Callback>::getReadBuffer() {
  if (shouldReadToAsciiBuffer()) {
    return asciiParser_.getReadBuffer();
  } else {
    return parser_.getReadBuffer();
  }
}

template <class Callback>
bool ServerMcParser<Callback>::readDataAvailable(size_t len) {
  if (shouldReadToAsciiBuffer()) {
    asciiParser_.readDataAvailable(len);
    return true;
  } else {
    return parser_.readDataAvailable(len);
  }
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
  if (UNLIKELY(parser_.protocol() != mc_ascii_protocol)) {
    std::string reason(
      folly::sformat("Expected {} protocol, but received ASCII!",
                     mc_protocol_to_string(parser_.protocol())));
    callback_.parseError(mc_res_local_error, reason);
    return;
  }

  // Note: McParser never chains IOBufs.
  auto result = asciiParser_.consume(readBuffer);

  if (result == McAsciiParserBase::State::ERROR) {
    // Note: we could include actual parsing error instead of
    // "malformed request" (e.g. asciiParser_.getErrorDescription()).
    callback_.parseError(mc_res_client_error,
                         "malformed request");
  }
}

template <class Callback>
void ServerMcParser<Callback>::parseError(mc_res_t result,
                                          folly::StringPiece reason) {
  callback_.parseError(result, reason);
}

template <class Callback>
bool ServerMcParser<Callback>::shouldReadToAsciiBuffer() const {
  return parser_.protocol() == mc_ascii_protocol &&
         asciiParser_.hasReadBuffer();
}

template <class Callback>
template <class Operation, class Request>
void ServerMcParser<Callback>::onRequest(Operation,
                                         Request&& req,
                                         bool noreply) {
  parser_.reportMsgRead();
  callback_.onRequest(Operation(), std::move(req), noreply);
}

template <class Callback>
void ServerMcParser<Callback>::multiOpEnd() {
  callback_.multiOpEnd();
}

}}  // facebook::memcache
