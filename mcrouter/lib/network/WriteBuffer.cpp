/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/network/McServerRequestContext.h"

#include "mcrouter/lib/mc/protocol.h"

namespace facebook { namespace memcache {

WriteBuffer::WriteBuffer(mc_protocol_t protocol)
    : protocol_(protocol) {
  switch (protocol_) {
    case mc_ascii_protocol:
      new (&asciiReply_) AsciiSerializedReply();
      break;

    case mc_umbrella_protocol:
      new (&umbrellaReply_) UmbrellaSerializedMessage();
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
      if (version_ == UmbrellaVersion::BASIC) {
        umbrellaReply_.~UmbrellaSerializedMessage();
      } else {
        caretReply_.~CaretSerializedMessage();
      }
      break;

    default:
      CHECK(false);
  }
}

void WriteBuffer::ensureType(UmbrellaVersion version) {
  if (version_ == version) {
    return;
  }
  if (version == UmbrellaVersion::TYPED_MESSAGE) {
    umbrellaReply_.~UmbrellaSerializedMessage();
    new (&caretReply_) CaretSerializedMessage();
  } else {
    caretReply_.~CaretSerializedMessage();
    new (&umbrellaReply_) UmbrellaSerializedMessage();
  }
  version_ = version;
}

void WriteBuffer::clear() {
  ctx_.clear();
  reply_.clear();

  switch (protocol_) {
    case mc_ascii_protocol:
      asciiReply_.clear();
      break;

    case mc_umbrella_protocol:
      if (version_ == UmbrellaVersion::BASIC) {
        umbrellaReply_.clear();
      } else {
        caretReply_.clear();
      }
      break;

    default:
      CHECK(false);
  }
}

bool WriteBuffer::prepare(McServerRequestContext&& ctx, McReply&& reply) {
  ctx_.emplace(std::move(ctx));
  reply_.emplace(std::move(reply));

  switch (protocol_) {
    case mc_ascii_protocol:
      return asciiReply_.prepare(reply_.value(),
                                 ctx_->operation_,
                                 ctx_->asciiKey(),
                                 iovsBegin_,
                                 iovsCount_);
      break;

    case mc_umbrella_protocol:
      ensureType(UmbrellaVersion::BASIC);
      return umbrellaReply_.prepare(reply_.value(),
                                    ctx_->operation_,
                                    ctx_->reqid_,
                                    iovsBegin_,
                                    iovsCount_);
      break;

    default:
      CHECK(false);
  }
}

AsciiSerializedReply::AsciiSerializedReply() {
  mc_ascii_response_buf_init(&asciiResponse_);
}

AsciiSerializedReply::~AsciiSerializedReply() {
  mc_ascii_response_buf_cleanup(&asciiResponse_);
}

void AsciiSerializedReply::clear() {
  mc_ascii_response_buf_cleanup(&asciiResponse_);
  mc_ascii_response_buf_init(&asciiResponse_);
}

bool AsciiSerializedReply::prepare(const McReply& reply,
                                   mc_op_t operation,
                                   const folly::Optional<folly::IOBuf>& key,
                                   struct iovec*& iovOut, size_t& niovOut) {
  mc_msg_t replyMsg;
  mc_msg_init_not_refcounted(&replyMsg);
  reply.dependentMsg(operation, &replyMsg);

  nstring_t k;
  if (key.hasValue()) {
    k.str = (char*)key->data();
    k.len = key->length();
  } else {
    k.str = nullptr;
    k.len = 0;
  }
  niovOut = mc_ascii_response_write_iovs(
    &asciiResponse_,
    k,
    operation,
    &replyMsg,
    iovs_,
    kMaxIovs);
  iovOut = iovs_;
  return niovOut != 0;
}

}}  // facebook::memcache
