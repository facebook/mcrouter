/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/fbi/cpp/LogFailure.h"

namespace facebook { namespace memcache {

template <class Callback>
ClientMcParser<Callback>::ClientMcParser(Callback& cb,
                                         size_t requestsPerRead,
                                         size_t minBufferSize,
                                         size_t maxBufferSize,
                                         bool useNewAsciiParser,
                                         mc_protocol_t protocol)
  : parser_(*this, requestsPerRead, minBufferSize, maxBufferSize),
    useNewParser_(useNewAsciiParser),
    protocol_(protocol),
    callback_(cb) {
  if (!useNewParser_) {
    mc_parser_init(&mcParser_,
                   reply_parser,
                   &parserMsgReady,
                   &parserParseError,
                   this);
  }
}

template <class Callback>
ClientMcParser<Callback>::~ClientMcParser() {
  if (!useNewParser_) {
    mc_parser_reset(&mcParser_);
  }
}

template <class Callback>
std::pair<void*, size_t> ClientMcParser<Callback>::getReadBuffer() {
  if (useNewParser_ && parser_.protocol() == mc_ascii_protocol &&
      asciiParser_.hasReadBuffer()) {
    return asciiParser_.getReadBuffer();
  } else {
    return parser_.getReadBuffer();
  }
}

template <class Callback>
bool ClientMcParser<Callback>::readDataAvailable(size_t len) {
  if (useNewParser_ && parser_.protocol() == mc_ascii_protocol &&
      asciiParser_.hasReadBuffer()) {
    asciiParser_.readDataAvailable(len);
    return true;
  } else {
    return parser_.readDataAvailable(len);
  }
}

template <class Callback>
template <class Operation, class Request>
void ClientMcParser<Callback>::expectNext() {
  if (useNewParser_ && parser_.protocol() == mc_ascii_protocol) {
    asciiParser_.initializeReplyParser<Operation, Request>();
    replyForwarder_ =
      &ClientMcParser<Callback>::forwardAsciiReply<Operation, Request>;
  } else if (parser_.protocol() == mc_umbrella_protocol) {
    umbrellaForwarder_ =
      &ClientMcParser<Callback>::forwardUmbrellaReply<Operation, Request>;
  }
}

template <class Callback>
void ClientMcParser<Callback>::replyReadyHelper(McReply&& reply,
                                                uint64_t reqid) {
  parser_.reportMsgRead();
  callback_.replyReady(std::move(reply), reqid);
}

template <class Callback>
template <class Operation, class Request>
void ClientMcParser<Callback>::forwardAsciiReply() {
  parser_.reportMsgRead();
  callback_.replyReady(
    asciiParser_.getReply<typename ReplyType<Operation,
                                             Request>::type>(), 0 /* reqId */);
  replyForwarder_ = nullptr;
}

template <class Callback>
template <class Operation, class Request>
void ClientMcParser<Callback>::forwardUmbrellaReply(
  const UmbrellaMessageInfo& info,
  const uint8_t* header,
  const uint8_t* body,
  const folly::IOBuf& bodyBuffer,
  uint64_t reqId) {

  if (info.version == UmbrellaVersion::BASIC) {
    auto reply = umbrellaParseReply<Operation, Request>(
        bodyBuffer, header, info.headerSize, body, info.bodySize);
    callback_.replyReady(std::move(reply), reqId);
  } else {
    ReplyT<Operation, Request> reply;
    folly::IOBuf trim;
    bodyBuffer.cloneOneInto(trim);
    trim.trimStart(info.headerSize);

    // Task: 8257655 - Conversion should be moved to ProxyDestination
    converter_.dispatchTypedRequest(info.typeId, trim, reply);
    callback_.replyReady(std::move(reply), reqId);
  }
}

template <class Callback>
bool ClientMcParser<Callback>::umMessageReady(const UmbrellaMessageInfo& info,
                                              const uint8_t* header,
                                              const uint8_t* body,
                                              const folly::IOBuf& bodyBuffer) {
  if (UNLIKELY(protocol_ != mc_umbrella_protocol)) {
    std::string reason =
        folly::sformat("Expected {} protocol, but received umbrella!",
                       mc_protocol_to_string(protocol_));
    callback_.parseError(mc_res_local_error, reason);
    return false;
  }

  try {
    size_t reqId;
    if (info.version == UmbrellaVersion::BASIC) {
      reqId = umbrellaDetermineReqId(header, info.headerSize);
    } else {
      reqId = info.reqId;
    }
    if (callback_.nextReplyAvailable(reqId)) {
      (this->*umbrellaForwarder_)(info, header, body, bodyBuffer, reqId);
    }
    // Consume the message, but don't fail.
    return true;
  } catch (const std::runtime_error& e) {
    std::string reason(
      std::string("Error parsing Umbrella message: ") + e.what());
    LOG(ERROR) << reason;
    callback_.parseError(mc_res_local_error, reason);
    return false;
  }
}

template <class Callback>
void ClientMcParser<Callback>::handleAscii(folly::IOBuf& readBuffer) {
  if (UNLIKELY(protocol_ != mc_ascii_protocol)) {
    std::string reason(
      folly::sformat("Expected {} protocol, but received ASCII!",
                     mc_protocol_to_string(protocol_)));
    callback_.parseError(mc_res_local_error, reason);
    return;
  }

  if (useNewParser_) {
    while (readBuffer.length()) {
      if (asciiParser_.getCurrentState() == McAsciiParser::State::UNINIT) {
        // Ask the client to initialize parser.
        if (!callback_.nextReplyAvailable(0 /* reqId */)) {
          auto data = reinterpret_cast<const char*>(readBuffer.data());
          std::string reason(
            folly::sformat(
              "Received unexpected data from remote endpoint: '{}'!",
              folly::cEscape<std::string>(folly::StringPiece(
                data, data + std::min(readBuffer.length(),
                static_cast<size_t>(128))))));
          callback_.parseError(mc_res_local_error, reason);
          return;
        }
      }
      switch (asciiParser_.consume(readBuffer)) {
        case McAsciiParser::State::COMPLETE:
          (this->*replyForwarder_)();
          break;
        case McAsciiParser::State::ERROR:
          callback_.parseError(mc_res_local_error,
                               asciiParser_.getErrorDescription());
          return;
          break;
        case McAsciiParser::State::PARTIAL:
          // Buffer was completely consumed.
          break;
        case McAsciiParser::State::UNINIT:
          // We fed parser some data, it shouldn't remain in State::NONE.
          callback_.parseError(mc_res_local_error,
                               "Sent data to AsciiParser but it remained in "
                               "UNINIT state!");
          return;
          break;
      }
    }
  } else {
    /* mc_parser only works with contiguous blocks */
    auto bytes = readBuffer.coalesce();
    mc_parser_parse(&mcParser_, bytes.begin(), bytes.size());
    readBuffer.clear();
  }
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
