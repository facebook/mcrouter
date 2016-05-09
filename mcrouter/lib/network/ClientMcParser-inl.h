/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/Cursor.h>
#include <thrift/lib/cpp2/protocol/CompactProtocol.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

namespace facebook { namespace memcache {

template <class Callback>
ClientMcParser<Callback>::ClientMcParser(
    Callback& cb,
    size_t minBufferSize,
    size_t maxBufferSize,
    const bool useJemallocNodumpAllocator,
    const CompressionCodecMap* compressionCodecMap)
    : parser_(
          *this,
          minBufferSize,
          maxBufferSize,
          useJemallocNodumpAllocator),
      callback_(cb),
      compressionCodecMap_(compressionCodecMap) {}

template <class Callback>
std::pair<void*, size_t> ClientMcParser<Callback>::getReadBuffer() {
  if (shouldReadToAsciiBuffer()) {
    return asciiParser_.getReadBuffer();
  } else {
    return parser_.getReadBuffer();
  }
}

template <class Callback>
bool ClientMcParser<Callback>::readDataAvailable(size_t len) {
  if (shouldReadToAsciiBuffer()) {
    asciiParser_.readDataAvailable(len);
    return true;
  } else {
    return parser_.readDataAvailable(len);
  }
}

template <class Callback>
template <class Request>
void ClientMcParser<Callback>::expectNext() {
  if (parser_.protocol() == mc_ascii_protocol) {
    asciiParser_.initializeReplyParser<Request>();
    replyForwarder_ = &ClientMcParser<Callback>::forwardAsciiReply<Request>;
  } else if (parser_.protocol() == mc_umbrella_protocol) {
    umbrellaOrCaretForwarder_ =
      &ClientMcParser<Callback>::forwardUmbrellaReply<Request>;
  } else if (parser_.protocol() == mc_caret_protocol) {
    umbrellaOrCaretForwarder_ =
      &ClientMcParser<Callback>::forwardCaretReply<Request>;
  }
}

template <class Callback>
template <class Request>
void ClientMcParser<Callback>::forwardAsciiReply() {
  callback_.replyReady(asciiParser_.getReply<ReplyT<Request>>(), 0 /* reqId */);
  replyForwarder_ = nullptr;
}

template <class Callback>
template <class Request>
void ClientMcParser<Callback>::forwardUmbrellaReply(
    const UmbrellaMessageInfo& info,
    const folly::IOBuf& buffer,
    uint64_t reqId) {

  auto reply = umbrellaParseReply<Request>(
      buffer,
      buffer.data(),
      info.headerSize,
      buffer.data() + info.headerSize,
      info.bodySize);

  callback_.replyReady(std::move(reply), reqId);
}

template <class Callback>
template <class Request>
typename std::enable_if<!IsCustomRequest<Request>::value, void>::type
ClientMcParser<Callback>::forwardCaretReply(
    const UmbrellaMessageInfo& headerInfo,
    const folly::IOBuf& buffer,
    uint64_t reqId) {

  const folly::IOBuf* finalBuffer = &buffer;

  // Uncompress if compressed
  std::unique_ptr<folly::IOBuf> uncompressedBuf;
  if (headerInfo.usedCodecId > 0) {
    uncompressedBuf = decompress(headerInfo, buffer);

    // Prepend a sentinel of the size of the header, as dispatchTypedRequest
    // expected a buffer containing the header and body.
    // TODO(aap): improve.
    auto headerBuf = folly::IOBuf::create(headerInfo.headerSize);
    headerBuf->append(headerInfo.headerSize);
    uncompressedBuf->prependChain(std::move(headerBuf));

    finalBuffer = uncompressedBuf->prev();
  }

  ReplyT<Request> reply;
  converter_.dispatchTypedRequest(headerInfo, *finalBuffer, reply);
  callback_.replyReady(std::move(reply), reqId);
}

template <class Callback>
template <class Request>
typename std::enable_if<IsCustomRequest<Request>::value, void>::type
ClientMcParser<Callback>::forwardCaretReply(
    const UmbrellaMessageInfo& headerInfo,
    const folly::IOBuf& buffer,
    uint64_t reqId) {

  const folly::IOBuf* finalBuffer = &buffer;
  size_t offset = headerInfo.headerSize;

  // Uncompress if compressed
  std::unique_ptr<folly::IOBuf> uncompressedBuf;
  if (headerInfo.usedCodecId > 0) {
    uncompressedBuf = decompress(headerInfo, buffer);
    finalBuffer = uncompressedBuf.get();
    offset = 0;
  }

  ReplyT<Request> reply;
  folly::io::Cursor cur(finalBuffer);
  cur += offset;
  apache::thrift::CompactProtocolReader reader;
  reader.setInput(cur);
  reply.read(&reader);

  callback_.replyReady(std::move(reply), reqId);
}

template <class Callback>
std::unique_ptr<folly::IOBuf> ClientMcParser<Callback>::decompress(
    const UmbrellaMessageInfo& headerInfo,
    const folly::IOBuf& buffer) {
  assert(compressionCodecMap_);
  assert(!buffer.isChained());
  auto codec = compressionCodecMap_->get(headerInfo.usedCodecId);
  assert(codec);

  auto buf = buffer.data() + headerInfo.headerSize;
  return codec->uncompress(
      buf, headerInfo.bodySize, headerInfo.uncompressedBodySize);
}

template <class Callback>
bool ClientMcParser<Callback>::umMessageReady(const UmbrellaMessageInfo& info,
                                              const folly::IOBuf& buffer) {
  if (UNLIKELY(parser_.protocol() != mc_umbrella_protocol)) {
    std::string reason =
        folly::sformat("Expected {} protocol, but received umbrella!",
                       mc_protocol_to_string(parser_.protocol()));
    callback_.parseError(mc_res_local_error, reason);
    return false;
  }

  try {
    const size_t reqId = umbrellaDetermineReqId(buffer.data(), info.headerSize);
    if (callback_.nextReplyAvailable(reqId)) {
      (this->*umbrellaOrCaretForwarder_)(info, buffer, reqId);
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
bool ClientMcParser<Callback>::caretMessageReady(
    const UmbrellaMessageInfo& headerInfo,
    const folly::IOBuf& buffer) {
  if (UNLIKELY(parser_.protocol() != mc_caret_protocol)) {
    const auto reason =
      folly::sformat("Expected {} protocol, but received Caret!",
                     mc_protocol_to_string(parser_.protocol()));
    callback_.parseError(mc_res_local_error, reason);
    return false;
  }

  try {
    const size_t reqId = headerInfo.reqId;
    if (callback_.nextReplyAvailable(reqId)) {
      (this->*umbrellaOrCaretForwarder_)(headerInfo, buffer, reqId);
    }
    return true;
  } catch (const std::runtime_error& e) {
    const auto reason =
      folly::sformat("Error parsing Caret message: {}", e.what());
    callback_.parseError(mc_res_local_error, reason);
    return false;
  }
}

template <class Callback>
void ClientMcParser<Callback>::handleAscii(folly::IOBuf& readBuffer) {
  if (UNLIKELY(parser_.protocol() != mc_ascii_protocol)) {
    std::string reason(
      folly::sformat("Expected {} protocol, but received ASCII!",
                     mc_protocol_to_string(parser_.protocol())));
    callback_.parseError(mc_res_local_error, reason);
    return;
  }

  while (readBuffer.length()) {
    if (asciiParser_.getCurrentState() == McAsciiParserBase::State::UNINIT) {
      // Ask the client to initialize parser.
      if (!callback_.nextReplyAvailable(0 /* reqId */)) {
        auto data = reinterpret_cast<const char *>(readBuffer.data());
        std::string reason(folly::sformat(
            "Received unexpected data from remote endpoint: '{}'!",
            folly::cEscape<std::string>(folly::StringPiece(
                data, data + std::min(readBuffer.length(),
                                      static_cast<size_t>(128))))));
        callback_.parseError(mc_res_local_error, reason);
        return;
      }
    }
    switch (asciiParser_.consume(readBuffer)) {
    case McAsciiParserBase::State::COMPLETE:
      (this->*replyForwarder_)();
      break;
    case McAsciiParserBase::State::ERROR:
      callback_.parseError(mc_res_local_error,
                           asciiParser_.getErrorDescription());
      return;
    case McAsciiParserBase::State::PARTIAL:
      // Buffer was completely consumed.
      break;
    case McAsciiParserBase::State::UNINIT:
      // We fed parser some data, it shouldn't remain in State::NONE.
      callback_.parseError(mc_res_local_error,
                           "Sent data to AsciiParser but it remained in "
                           "UNINIT state!");
      return;
    }
  }
}

template <class Callback>
void ClientMcParser<Callback>::parseError(mc_res_t result,
                                          folly::StringPiece reason) {
  callback_.parseError(result, reason);
}

template <class Callback>
bool ClientMcParser<Callback>::shouldReadToAsciiBuffer() const {
  return parser_.protocol() == mc_ascii_protocol &&
         asciiParser_.hasReadBuffer();
}

}}  // facebook::memcache
