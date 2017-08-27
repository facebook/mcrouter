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

  if (getMagic() != 0x80 || getDataType() != 0x00) {
    return false;
  }

  // TODO validate command constraint (i.e. no extras, no value)
  switch (getOpCode()) {
    case mc_opcode_set:
      currentMessage_.emplace<McSetRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McSetRequest, false>;
      return true;
    case mc_opcode_setq:
      currentMessage_.emplace<McSetRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McSetRequest, true>;
      return true;
    case mc_opcode_add:
      currentMessage_.emplace<McAddRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McAddRequest, false>;
      return true;
    case mc_opcode_addq
      currentMessage_.emplace<McAddRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McAddRequest, true>;
      return true;
    case mc_opcode_replace:
      currentMessage_.emplace<McReplaceRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McReplaceRequest, false>;
      return true;
    case mc_opcode_replaceq:
      currentMessage_.emplace<McReplaceRequest>();
      consumer_ = &McServerBinaryParser::consumeSetLike<McReplaceRequest, true>;
      return true;
    case mc_opcode_append:
      currentMessage_.emplace<McAppendRequest>();
      consumer_ = &McServerBinaryParser::consumeAppendLike<McAppendRequest, false>;
      return true;
    case mc_opcode_appendq:
      currentMessage_.emplace<McAppendRequest>();
      consumer_ = &McServerBinaryParser::consumeAppendLike<McAppendRequest, true>;
      return true;
    case mc_opcode_prepend:
      currentMessage_.emplace<McPrependRequest>();
      consumer_ = &McServerBinaryParser::consumeAppendLike<McPrependRequest, false>;
      return true;
    case mc_opcode_prependq:
      currentMessage_.emplace<McPrependRequest>();
      consumer_ = &McServerBinaryParser::consumeAppendLike<McPrependRequest, true>;
      return true;
    case mc_opcode_get:
      currentMessage_.emplace<McGetRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McGetRequest, false, false>;
      return true;
    case mc_opcode_getq:
      currentMessage_.emplace<McGetRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McGetRequest, true, false>;
      return true;
    case mc_opcode_getk:
      currentMessage_.emplace<McGetRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McGetRequest, false, true>;
      return true;
    case mc_opcode_getkq:
      currentMessage_.emplace<McGetRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McGetRequest, true, true>;
      return true;
    case mc_opcode_delete:
      currentMessage_.emplace<McDeleteRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McDeleteRequest, false, false>;
      return true;
    case mc_opcode_deleteq:
      currentMessage_.emplace<McDeleteRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McDeleteRequest, true, false>;
      return true;
    case mc_opcode_increment:
      currentMessage_.emplace<McIncrRequest>();
      consumer_ = &McServerBinaryParser::consumeArithLike<McIncrRequest, false>;
      return true;
    case mc_opcode_incrementq:
      currentMessage_.emplace<McIncrRequest>();
      consumer_ = &McServerBinaryParser::consumeArithLike<McIncrRequest, true>;
      return true;
    case mc_opcode_decrement:
      currentMessage_.emplace<McDecrRequest>();
      consumer_ = &McServerBinaryParser::consumeArithLike<McDecrRequest, false>;
      return true;
    case mc_opcode_decrementq:
      currentMessage_.emplace<McDecrRequest>();
      consumer_ = &McServerBinaryParser::consumeArithLike<McDecrRequest, true>;
      return true;
    case mc_opcode_touch:
    case mc_opcode_gat:
    case mc_opcode_gatq:
      currentMessage_.emplace<McTouchRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McTouchRequest, false, false>;
      return true;
    case mc_opcode_stat:
      currentMessage_.emplace<McStatsRequest>();
      consumer_ = &McServerBinaryParser::consumeGetLike<McStatsRequest, false, false>;
      return true;
    case mc_opcode_version:
      currentMessage_.emplace<McVersionRequest>();
      consumer_ = &McServerBinaryParser::consumeVersion;
      return true;
    case mc_opcode_quit:
      currentMessage_.emplace<McQuitRequest>();
      consumer_ = &McServerBinaryParser::consumeQuit<false>;
      return true;
    case mc_opcode_quitq:
      currentMessage_.emplace<McQuitRequest>();
      consumer_ = &McServerBinaryParser::consumeQuit<true>;
      return true;
    case mc_opcode_flush:
      currentMessage_.emplace<McFlushAllRequest>();
      consumer_ = &McServerBinaryParser::consumeFlush<false>;
      return true;
    case mc_opcode_flushq:
      currentMessage_.emplace<McFlushAllRequest>();
      consumer_ = &McServerBinaryParser::consumeFlush<true>;
      return true;
    case mc_opcode_noop:
    // SASL commands
    case mc_opcode_sasllistmechs:
    case mc_opcode_saslauth:
    case mc_opcode_saslstep:
    // Range commands
    case mc_opcode_rset:
    case mc_opcode_rsetq:
    case mc_opcode_rappend:
    case mc_opcode_rappendq:
    case mc_opcode_rprepend:
    case mc_opcode_rprependq:
    case mc_opcode_rget:
    case mc_opcode_rdelete:
    case mc_opcode_rdeleteq:
    case mc_opcode_rincr:
    case mc_opcode_rincrq:
    case mc_opcode_rdecr:
    case mc_opcode_rdecrq:
    // v1.6 proposed commands
    case mc_opcode_setvbucket:
    case mc_opcode_tapvbucketset:
    case mc_opcode_getvbucket:
    case mc_opcode_tapdelete:
    case mc_opcode_verosity:
    case mc_opcode_tapflush:
    case mc_opcode_delvbucket:
    case mc_opcode_tapconnect:
    case mc_opcode_tapmutation:
    case mc_opcode_tapopaque:
    case mc_opcode_tapcheckpointstart:
    case mc_opcode_tapcheckpointend::
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
