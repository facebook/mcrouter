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
    case 0x11:  // SetQ
    case 0x31:  // RSet
    case 0x32:  // RSetQ
      // TODO check the necessary parameters (extras size)
      currentMessage_.emplace<McSetRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McSetRequest>;
      return true;
    case 0x02:  // Add
    case 0x12:  // AddQ
      currentMessage_.emplace<McAddRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McAddRequest>;
      return true;
    case 0x03:  // Replace
    case 0x13:  // ReplaceQ
      currentMessage_.emplace<McReplaceRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McReplaceRequest>;
      return true;
    case 0x0e:  // Append
    case 0x19:  // AppendQ
    case 0x33:  // RAppend
    case 0x34:  // RAppendQ
      currentMessage_.emplace<McAppendRequest>();
      consumer_ = &McServerBinaryParser::consumeAppendLike<McAppendRequest>;
      return true;
    case 0x0f:  // Prepend
    case 0x1a:  // PrependQ
    case 0x35:  // RPrepend
    case 0x36:  // RPrependQ
      currentMessage_.emplace<McPrependRequest>();
      consumer_ = &McServerBinaryParser::consumeAppendLike<McPrependRequest>;
      return true;
    case 0x00:  // Get
    case 0x09:  // GetQ
    case 0x0c:  // GetK
    case 0x0d:  // GetKQ
    case 0x30:  // RGet
      currentMessage_.emplace<McGetRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McGetRequest>;
      return true;
    case 0x04:  // Delete
    case 0x14:  // DeleteQ
    case 0x37:  // RDelete
    case 0x38:  // RDeleteQ
      currentMessage_.emplace<McDeleteRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McDeleteRequest>;
      return true;
    case 0x05:  // Increment
    case 0x15:  // IncrementQ
    case 0x39:  // RIncr
    case 0x3a:  // RIncrQ
      currentMessage_.emplace<McIncrRequest>();
      consumer_ = &McServerBinaryParser::consumeArithLike<McIncrRequest>;
      return true;
    case 0x06:  // Decrement
    case 0x16:  // DecrementQ
    case 0x3b:  // RDecr
    case 0x3c:  // RDecrQ
      currentMessage_.emplace<McDecrRequest>();
      consumer_ = &McServerBinaryParser::consumeArithLike<McDecrRequest>;
      return true;
    case 0x1c:  // Touch
    case 0x1d:  // GAT
    case 0x1e:  // GATQ
      currentMessage_.emplace<McTouchRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McTouchRequest>;
      return true;
    case 0x10:  // Stat
      currentMessage_.emplace<McStatsRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McStatsRequest>;
      return true;
    case 0x0b:  // Version
      currentMessage_.emplace<McVersionRequest>();
      consumer_ = &McServerBinaryParser::consumeUnary<McVersionRequest>;
      return true;
    case 0x07:  // Quit
    case 0x17:  // QuitQ
      currentMessage_.emplace<McQuitRequest>();
      consumer_ = &McServerBinaryParser::consumeUnary<McQuitRequest>;
      return true;
    case 0x08:  // Flush
    case 0x18:  // FlushQ
      currentMessage_.emplace<McFlushAllRequest>();
      consumer_ = &McServerBinaryParser::consumeFlush;
      return true;
    /*
    case 0x20:  // SASL list mechs
    case 0x21:  // SASL Auth
    case 0x22:  // SASL Step

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

template <class Request>
void McServerBinaryParser::consumeSetLike() {
  auto extras       = reinterpret_cast<const SetExtras_t*>(currentExtras_.data());
  auto& message     = currentMessage_.get<Request>();
  message.key()     = std::move(currentKey_);
  message.exptime() = ntohl(extras->exptime);
  callback_->onRequest(std::move(message));
}

template <class Request>
void McServerBinaryParser::consumeAppendLike() {
  auto& message   = currentMessage_.get<Request>();
  message.key()   = std::move(currentKey_);
  message.value() = std::move(currentValue_);
  callback_->onRequest(std::move(message));
}

template <class Request>
void McServerBinaryParser::consumeGetLike() {
  auto& message   = currentMessage_.get<Request>();
  message.key()   = std::move(currentKey_);
  callback_->onRequest(std::move(message));
}

template <class Request>
void McServerBinaryParser::consumeArithLike() {
  auto extras     = reinterpret_cast<const ArithExtras_t*>(currentExtras_.data());
  auto& message   = currentMessage_.get<Request>();
  message.key()   = std::move(currentKey_);
  message.delta() = ntohl(extras->delta);
  // message.initialValue() = ntohl(extras->initialValue);
  // message.exptime() = ntohl(extras->exptime);
  callback_->onRequest(std::move(message));
}

template <class Request>
void McServerBinaryParser::consumeUnary() {
  auto& message = currentMessage_.get<Request>();
  callback_->onRequest(std::move(message));
}

void McServerBinaryParser::consumeFlush() {
  // auto extras       = reinterpret_cast<const FlushExtras_t*>(currentExtras_.data());
  auto& message     = currentMessage_.get<McFlushAllRequest>();
  // message.exptime() = ntohl(extras->exptime);
  callback_->onRequest(std::move(message));
}

}
} // facebook::memcache
