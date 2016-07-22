/*
 *  Copyright (c) 2016, Facebook, Inc.
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
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/MultiOpParent.h"

namespace facebook { namespace memcache {

WriteBuffer::WriteBuffer(mc_protocol_t protocol)
    : protocol_(protocol) {
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
  return ctx_.hasValue() &&
      (ctx_->hasParent() || ctx_->operation_ == mc_op_end);
}

mc_op_t WriteBuffer::operation() const {
  return ctx_.hasValue() ? ctx_->operation_ : mc_op_unknown;
}

}}  // facebook::memcache
