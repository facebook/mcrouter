/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/network/McAsciiParser.h"

#include <folly/String.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/McReply.h"

namespace facebook { namespace memcache {

constexpr size_t kProtocolTailContextLength = 128;

McAsciiParser::McAsciiParser() : state_(State::UNINIT) {
}

McAsciiParser::State McAsciiParser::consume(folly::IOBuf& buffer) {
  assert(state_ == State::PARTIAL && !hasReadBuffer());

  p_ = reinterpret_cast<const char*>(buffer.data());
  pe_ = p_ + buffer.length();
  eof_ = nullptr;

  (this->*consumer_)(buffer);

  if (savedCs_ == errorCs_) {
    handleError(buffer);
  }

  buffer.trimStart(p_ - reinterpret_cast<const char*>(buffer.data()));

  return state_;
}

bool McAsciiParser::hasReadBuffer() const {
  return state_ == State::PARTIAL && currentIOBuf_ != nullptr;
}

std::pair<void*, size_t> McAsciiParser::getReadBuffer() {
  assert(state_ == State::PARTIAL && currentIOBuf_ != nullptr);

  return std::make_pair(currentIOBuf_->writableTail(), remainingIOBufLength_);
}

void McAsciiParser::readDataAvailable(size_t length) {
  assert(state_ == State::PARTIAL && currentIOBuf_ != nullptr);
  assert(length <= remainingIOBufLength_);
  currentIOBuf_->append(length);
  remainingIOBufLength_ -= length;
  // We finished reading value.
  if (remainingIOBufLength_ == 0) {
    currentIOBuf_ = nullptr;
  }
}

void McAsciiParser::appendCurrentCharTo(folly::IOBuf& from, folly::IOBuf& to) {
  // If it is just a next char in the same memory chunk, just append it.
  // Otherwise we need to append new IOBuf.
  if (to.data() + to.length() ==
        reinterpret_cast<const void*>(p_) && to.tailroom() > 0) {
    to.append(1);
  } else {
    auto nextPiece = from.cloneOne();
    size_t offset = p_ - reinterpret_cast<const char*>(from.data());
    nextPiece->trimStart(offset);
    nextPiece->trimEnd(from.length() - offset - 1 /* current char */);
    to.appendChain(std::move(nextPiece));
  }
}

void McAsciiParser::handleError(folly::IOBuf& buffer) {
  state_ = State::ERROR;
  // We've encoutered error we need to do proper logging.
  auto start = reinterpret_cast<const char*>(buffer.data());
  auto length = std::min(p_ - start + kProtocolTailContextLength,
                         buffer.length());

  currentErrorDescription_ =
    folly::sformat("Error parsing message '{}' at character {}!",
                   folly::cEscape<std::string>(
                     folly::StringPiece(start, start + length)),
                   p_ - start);
}

folly::StringPiece McAsciiParser::getErrorDescription() const {
  return currentErrorDescription_;
}

}}  // facebook::memcache
