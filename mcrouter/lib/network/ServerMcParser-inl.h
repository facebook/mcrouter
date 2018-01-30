/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/lang/Bits.h>

#include "mcrouter/lib/debug/ConnectionFifo.h"
#include "mcrouter/lib/network/UmbrellaProtocol.h"

namespace facebook {
namespace memcache {

template <class Callback>
ServerMcParser<Callback>::ServerMcParser(
    Callback& cb,
    size_t minBufferSize,
    size_t maxBufferSize,
    ConnectionFifo* debugFifo)
    : parser_(
          *this,
          minBufferSize,
          maxBufferSize,
          /* useJemallocNodumpAllocator */ false,
          debugFifo),
      asciiParser_(*this),
      callback_(cb),
      debugFifo_(debugFifo) {}

template <class Callback>
ServerMcParser<Callback>::~ServerMcParser() {}

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
template <class Request>
void ServerMcParser<Callback>::requestReadyHelper(
    Request&& req,
    uint64_t reqid) {
  callback_.umbrellaRequestReady(std::move(req), reqid);
}

template <class Callback>
bool ServerMcParser<Callback>::umMessageReady(
    const UmbrellaMessageInfo& info,
    const folly::IOBuf& buffer) {
  try {
    uint64_t reqid;
    const mc_op_t op =
        umbrellaDetermineOperation(buffer.data(), info.headerSize);
    switch (op) {
#define THRIFT_OP(MC_OPERATION)                                           \
  case MC_OPERATION::mc_op: {                                             \
    using Request =                                                       \
        typename TypeFromOp<MC_OPERATION::mc_op, RequestOpMapping>::type; \
    auto req = umbrellaParseRequest<Request>(                             \
        buffer,                                                           \
        buffer.data(),                                                    \
        info.headerSize,                                                  \
        buffer.data() + info.headerSize,                                  \
        info.bodySize,                                                    \
        reqid);                                                           \
    requestReadyHelper(std::move(req), reqid);                            \
    break;                                                                \
  }
#include "mcrouter/lib/McOpList.h"
      default:
        auto reason = folly::sformat(
            "Error parsing Umbrella message. "
            "Unexpected Umbrella message of type: {} ({}).",
            mc_op_to_string(op),
            int(op));
        callback_.parseError(mc_res_remote_error, reason);
        return false;
    }
  } catch (const std::exception& e) {
    std::string reason(
        std::string("Error parsing Umbrella message: ") + e.what());
    callback_.parseError(mc_res_remote_error, reason);
    return false;
  }
  return true;
}

template <class Callback>
bool ServerMcParser<Callback>::caretMessageReady(
    const UmbrellaMessageInfo& headerInfo,
    const folly::IOBuf& buffer) {
  try {
    // Caret header and body are assumed to be in one coalesced IOBuf
    callback_.caretRequestReady(headerInfo, buffer);
  } catch (const std::exception& e) {
    std::string reason(std::string("Error parsing Caret message: ") + e.what());
    callback_.parseError(mc_res_remote_error, reason);
    return false;
  }
  return true;
}

template <class Callback>
void ServerMcParser<Callback>::handleAscii(folly::IOBuf& readBuffer) {
  if (UNLIKELY(parser_.protocol() != mc_ascii_protocol)) {
    std::string reason(folly::sformat(
        "Expected {} protocol, but received ASCII!",
        mc_protocol_to_string(parser_.protocol())));
    callback_.parseError(mc_res_local_error, reason);
    return;
  }

  // Note: McParser never chains IOBufs.
  auto result = asciiParser_.consume(readBuffer);

  if (result == McAsciiParserBase::State::ERROR) {
    // Note: we could include actual parsing error instead of
    // "malformed request" (e.g. asciiParser_.getErrorDescription()).
    callback_.parseError(mc_res_client_error, "malformed request");
  }
}

template <class Callback>
void ServerMcParser<Callback>::parseError(
    mc_res_t result,
    folly::StringPiece reason) {
  callback_.parseError(result, reason);
}

template <class Callback>
bool ServerMcParser<Callback>::shouldReadToAsciiBuffer() const {
  return parser_.protocol() == mc_ascii_protocol &&
      asciiParser_.hasReadBuffer();
}

template <class Callback>
template <class Request>
void ServerMcParser<Callback>::onRequest(Request&& req, bool noreply) {
  if (UNLIKELY(debugFifo_ && debugFifo_->isConnected())) {
    writeToPipe(req);
  }
  callback_.onRequest(std::move(req), noreply);
}

template <class Callback>
void ServerMcParser<Callback>::multiOpEnd() {
  callback_.multiOpEnd();
}

template <class Callback>
template <class Request>
void ServerMcParser<Callback>::writeToPipe(const Request& req) {
  assert(debugFifo_);
  AsciiSerializedRequest debugSerializedRequest;
  const struct iovec* iov;
  size_t iovLen;
  debugSerializedRequest.prepare(req, iov, iovLen);
  debugFifo_->startMessage(MessageDirection::Received, Request::typeId);
  debugFifo_->writeData(iov, iovLen);
}
}
} // facebook::memcache
