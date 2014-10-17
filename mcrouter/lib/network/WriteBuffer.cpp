/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "WriteBuffer.h"

#include "mcrouter/lib/mc/protocol.h"

namespace facebook { namespace memcache {

WriteBuffer::WriteBuffer() {
  mc_msg_init_not_refcounted(&replyMsg_);
  um_backing_msg_init(&umMsg_);
  mc_ascii_response_buf_init(&asciiResponse_);
}

void WriteBuffer::clear() {
  ctx_.clear();
  reply_.clear();
  um_backing_msg_cleanup(&umMsg_);
  um_backing_msg_init(&umMsg_);
  mc_ascii_response_buf_cleanup(&asciiResponse_);
  mc_ascii_response_buf_init(&asciiResponse_);
}

bool WriteBuffer::prepare(McServerRequestContext&& ctx, McReply&& reply,
                          mc_protocol_t protocol,
                          struct iovec*& iovOut, size_t& niovOut) {
  ctx_.emplace(std::move(ctx));
  reply_.emplace(std::move(reply));
  reply_->dependentMsg(ctx_->operation_, &replyMsg_);

  nstring_t k;
  if (ctx_->key_.hasValue()) {
    k.str = (char*)ctx_->key_->data();
    k.len = ctx_->key_->length();
  } else {
    k.str = nullptr;
    k.len = 0;
  }

  if (protocol == mc_ascii_protocol) {
    niovs_ = mc_ascii_response_write_iovs(
      &asciiResponse_,
      k,
      ctx_->operation_,
      &replyMsg_,
      iovs_,
      kMaxIovs);
  } else if (protocol == mc_umbrella_protocol) {
    auto niovs = um_write_iovs(&umMsg_, ctx_->reqid_,
                               &replyMsg_,
                               iovs_, kMaxIovs);
    niovs_ = (niovs == -1 ? 0 : niovs);
  } else {
    CHECK(false) << "Unknown protocol";
  }

  iovOut = iovs_;
  niovOut = niovs_;

  return niovs_ != 0;
}

}}  // facebook::memcache
