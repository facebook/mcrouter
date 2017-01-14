/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Conv.h>
#include <folly/String.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/MultiOpParent.h"

namespace facebook {
namespace memcache {

WriteBuffer::WriteBuffer(mc_protocol_t protocol) : protocol_(protocol) {
  switch (protocol_) {
    case mc_ascii_protocol:
      new (&asciiReply_) AsciiSerializedReply;
      break;

    case mc_umbrella_protocol:
      new (&umbrellaReply_) UmbrellaSerializedMessage;
      break;

    case mc_caret_protocol:
      new (&caretReply_) CaretSerializedMessage;
      break;

    default:
      CHECK(false) << "Unknown protocol";
  }
}

WriteBuffer::~WriteBuffer() {
  switch (protocol_) {
    case mc_ascii_protocol:
      asciiReply_.~AsciiSerializedReply();
      break;

    case mc_umbrella_protocol:
      umbrellaReply_.~UmbrellaSerializedMessage();
      break;

    case mc_caret_protocol:
      caretReply_.~CaretSerializedMessage();
      break;

    default:
      CHECK(false);
  }
}

void WriteBuffer::clear() {
  ctx_.clear();
  destructor_.clear();
  isEndOfBatch_ = false;
  typeId_ = 0;

  switch (protocol_) {
    case mc_ascii_protocol:
      asciiReply_.clear();
      break;

    case mc_umbrella_protocol:
      umbrellaReply_.clear();
      break;

    case mc_caret_protocol:
      caretReply_.clear();
      break;

    default:
      CHECK(false);
  }
}

bool WriteBuffer::noReply() const {
  return ctx_.hasValue() && ctx_->hasParent() && ctx_->parent().error();
}

bool WriteBuffer::isSubRequest() const {
  return ctx_.hasValue() && (ctx_->hasParent() || ctx_->isEndContext());
}

bool WriteBuffer::isEndContext() const {
  return ctx_.hasValue() ? ctx_->isEndContext() : false;
}

WriteBuffer::Queue& WriteBufferQueue::initFreeQueue(
    mc_protocol_t protocol) noexcept {
  assert(
      protocol == mc_ascii_protocol || protocol == mc_umbrella_protocol ||
      protocol == mc_caret_protocol);

  static thread_local WriteBuffer::Queue freeQ[mc_nprotocols];
  return freeQ[static_cast<size_t>(protocol)];
}
}
} // facebook::memcache
