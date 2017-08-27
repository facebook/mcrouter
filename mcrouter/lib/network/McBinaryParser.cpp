/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McBinaryParser.h"

#include <folly/String.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/IOBufUtil.h"

namespace facebook {
namespace memcache {

McServerBinaryParser::State McServerBinaryParser::consume(folly::IOBuf& buffer) {
  assert(state_ != State::ERROR);
  assert(state_ != State::COMPLETE);

  uint64_t avaiBytes = buffer.length();
  const char *p_ = reinterpret_cast<const char*>(buffer.data());

  if (state_ == State::UNINIT) {
    header_        = nullptr;
    sectionLength_ = HeaderLength;
    sectionStart_  = p_;
    state_         = State::PARTIAL_HEADER;
  } else {
    while (state_ != State::ERROR && state_ != State::COMPLETE
      && avaiBytes >= sectionLength_) {
      switch (state_) {
        case State::PARTIAL_HEADER:
          if (!parseHeader(p_)) {
            state_ = State::ERROR;
          } else {
            sectionStart_ += sectionLength_;
            sectionLength_ = getExtrasLength();
            state_ = State::PARTIAL_EXTRA;
          }
          break;

        case State::PARTIAL_EXTRA:
          appendKeyPiece(
            buffer, currentValue_,
            sectionStart_, sectionStart_ + sectionLength_);
          sectionStart_ += sectionLength_;
          sectionLength_ = getKeyLength();
          state_ = State::PARTIAL_KEY;
          break;

        case State::PARTIAL_KEY:
          appendKeyPiece(
            buffer, currentValue_,
            sectionStart_, sectionStart_ + sectionLength_);
          sectionStart_ += sectionLength_;
          sectionLength_ = getValueLength();
          state_ = State::PARTIAL_VALUE;
          break;

        case State::PARTIAL_VALUE:
          appendKeyPiece(
            buffer, currentValue_,
            sectionStart_, sectionStart_ + sectionLength_);
          sectionStart_ += sectionLength_;
          state_ = State::COMPLETE;
          (this->*consumer_)();
          break;

        default:
          CHECK(false);
      }
    }
  }
  buffer.trimStart(sectionStart_ - p_);

  return state_;
}

bool McServerBinaryParser::parseHeader(const char * bytes) {
  header_ = reinterpret_cast<const RequestHeader*>(bytes);

  if (getMagic() != 0x80 || getMagic() != 0x81 || getDataType() != 0x00) {
    return false;
  }

  // TODO validate command constraint (i.e. no extras, no value)
  switch (getOpCode()) {
    case 0x0a:  // No-op
      return false;

    case 0x01:  // Set
      currentMessage_.emplace<McSetRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McSetRequest, false>;
      return true;
    case 0x11:  // SetQ
      currentMessage_.emplace<McSetRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McSetRequest, true>;
      return true;
    case 0x02:  // Add
      currentMessage_.emplace<McAddRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McAddRequest, false>;
      return true;
    case 0x12:  // AddQ
      currentMessage_.emplace<McAddRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McAddRequest, true>;
      return true;
    case 0x03:  // Replace
      currentMessage_.emplace<McReplaceRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McReplaceRequest, false>;
      return true;
    case 0x13:  // ReplaceQ
      currentMessage_.emplace<McReplaceRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McReplaceRequest, true>;
      return true;
    case 0x0e:  // Append
      currentMessage_.emplace<McAppendRequest>();
      consumer_ = &McServerBinaryParser::consumeAppendLike<McAppendRequest, false>;
      return true;
    case 0x19:  // AppendQ
      currentMessage_.emplace<McAppendRequest>();
      consumer_ = &McServerBinaryParser::consumeAppendLike<McAppendRequest, true>;
      return true;
    case 0x0f:  // Prepend
      currentMessage_.emplace<McPrependRequest>();
      consumer_ = &McServerBinaryParser::consumeAppendLike<McPrependRequest, false>;
      return true;
    case 0x1a:  // PrependQ
      currentMessage_.emplace<McPrependRequest>();
      consumer_ = &McServerBinaryParser::consumeAppendLike<McPrependRequest, true>;
      return true;
    case 0x00:  // Get
      currentMessage_.emplace<McGetRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McGetRequest, false, false>;
      return true;
    case 0x09:  // GetQ
      currentMessage_.emplace<McGetRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McGetRequest, true, false>;
      return true;
    case 0x0c:  // GetK
      currentMessage_.emplace<McGetRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McGetRequest, false, true>;
      return true;
    case 0x0d:  // GetKQ
      currentMessage_.emplace<McGetRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McGetRequest, true, true>;
      return true;
    case 0x04:  // Delete
      currentMessage_.emplace<McDeleteRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McDeleteRequest, false, false>;
      return true;
    case 0x14:  // DeleteQ
      currentMessage_.emplace<McDeleteRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McDeleteRequest, true, false>;
      return true;
    case 0x05:  // Increment
      currentMessage_.emplace<McIncrRequest>();
      consumer_ = &McServerBinaryParser::consumeArithLike<McIncrRequest, false>;
      return true;
    case 0x15:  // IncrementQ
      currentMessage_.emplace<McIncrRequest>();
      consumer_ = &McServerBinaryParser::consumeArithLike<McIncrRequest, true>;
      return true;
    case 0x06:  // Decrement
      currentMessage_.emplace<McDecrRequest>();
      consumer_ = &McServerBinaryParser::consumeArithLike<McDecrRequest, false>;
      return true;
    case 0x16:  // DecrementQ
      currentMessage_.emplace<McDecrRequest>();
      consumer_ = &McServerBinaryParser::consumeArithLike<McDecrRequest, true>;
      return true;
    case 0x1c:  // Touch
    case 0x1d:  // GAT
    case 0x1e:  // GATQ
      currentMessage_.emplace<McTouchRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McTouchRequest, false, false>;
      return true;
    case 0x10:  // Stat
      currentMessage_.emplace<McStatsRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McStatsRequest, false, false>;
      return true;
    case 0x0b:  // Version
      currentMessage_.emplace<McVersionRequest>();
      consumer_ = &McServerBinaryParser::consumeVersion;
      return true;
    case 0x07:  // Quit
      currentMessage_.emplace<McQuitRequest>();
      consumer_ = &McServerBinaryParser::consumeQuit<false>;
      return true;
    case 0x17:  // QuitQ
      currentMessage_.emplace<McQuitRequest>();
      consumer_ = &McServerBinaryParser::consumeQuit<true>;
      return true;
    case 0x08:  // Flush
      currentMessage_.emplace<McFlushAllRequest>();
      consumer_ = &McServerBinaryParser::consumeFlush<false>;
      return true;
    case 0x18:  // FlushQ
      currentMessage_.emplace<McFlushAllRequest>();
      consumer_ = &McServerBinaryParser::consumeFlush<true>;
      return true;
    /*
    case 0x20:  // SASL list mechs
    case 0x21:  // SASL Auth
    case 0x22:  // SASL Step

    // Range operations, not implemented in memcached itself
    case 0x31:  // RSet
    case 0x32:  // RSetQ
    case 0x33:  // RAppend
    case 0x34:  // RAppendQ
    case 0x35:  // RPrepend
    case 0x36:  // RPrependQ
    case 0x30:  // RGet
    case 0x37:  // RDelete
    case 0x38:  // RDeleteQ
    case 0x39:  // RIncr
    case 0x3a:  // RIncrQ
    case 0x3b:  // RDecr
    case 0x3c:  // RDecrQ

    // v1.6 proposed commands
    case 0x3d:  // Set VBucket *
    case 0x45:  // TAP VBucket Set *
    case 0x3e:  // Get VBucket *
    case 0x42:  // TAP Delete *

    case 0x1b:  // Verbosity *
    case 0x43:  // TAP Flush *
    case 0x3f:  // Del VBucket *
    case 0x40:  // TAP Connect *
    case 0x41:  // TAP Mutation *
    case 0x44:  // TAP Opaque *
    case 0x46:  // TAP Checkpoint Start *
    case 0x47:  // TAP Checkpoint End *
    */
    default:
      return false;
  }
}

template <class Request, bool quiet>
void McServerBinaryParser::consumeSetLike() {
  auto extras       = reinterpret_cast<const SetExtras_t*>(currentExtras_.data());
  auto& message     = currentMessage_.get<Request>();
  message.key()     = std::move(currentKey_);
  message.exptime() = ntohl(extras->exptime);
  message.quiet()   = quiet;
  callback_->onRequest(std::move(message));
}

template <class Request, bool quiet>
void McServerBinaryParser::consumeAppendLike() {
  auto& message   = currentMessage_.get<Request>();
  message.key()   = std::move(currentKey_);
  message.value() = std::move(currentValue_);
  message.quiet() = quiet;
  callback_->onRequest(std::move(message));
}

template <class Request, bool quiet, bool returnKey>
void McServerBinaryParser::consumeGetLike() {
  auto& message       = currentMessage_.get<Request>();
  message.key()       = std::move(currentKey_);
  message.quiet()     = quiet;
  message.returnKey() = returnKey;
  callback_->onRequest(std::move(message));
}

template <class Request, bool quiet>
void McServerBinaryParser::consumeArithLike() {
  auto extras            = reinterpret_cast<const ArithExtras_t*>(currentExtras_.data());
  auto& message          = currentMessage_.get<Request>();
  message.key()          = std::move(currentKey_);
  message.delta()        = ntohl(extras->delta);
  // These fields are for binary protocol only, we cannot forward them to
  // upstream servers because we use the ASCII protocol for upstreams
  // message.initialValue() = ntohl(extras->initialValue);
  // message.exptime() = ntohl(extras->exptime);
  message.quiet()        = quiet;
  callback_->onRequest(std::move(message));
}

template <bool quiet>
void McServerBinaryParser::consumeQuit() {
  auto& message   = currentMessage_.get<McQuitRequest>();
  message.quiet() = quiet;
  callback_->onRequest(std::move(message));
}

void McServerBinaryParser::consumeVersion() {
  auto& message = currentMessage_.get<McVersionRequest>();
  callback_->onRequest(std::move(message));
}

template <bool quiet>
void McServerBinaryParser::consumeFlush() {
  // auto extras       = reinterpret_cast<const FlushExtras_t*>(currentExtras_.data());
  auto& message     = currentMessage_.get<McFlushAllRequest>();
  // Binary protocol only fields
  // message.exptime() = ntohl(extras->exptime);
  message.quiet()   = quiet;
  callback_->onRequest(std::move(message));
}

}
} // facebook::memcache
