/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <folly/Optional.h>

#include "mcrouter/lib/mc/ascii_response.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/UniqueIntrusiveList.h"
#include "mcrouter/lib/network/McServerRequestContext.h"

namespace facebook { namespace memcache {

class McServerSession;

class WriteBuffer {
 private:
  UniqueIntrusiveListHook hook_;

 public:
  using Queue = UniqueIntrusiveList<WriteBuffer,
                                    &WriteBuffer::hook_>;

  WriteBuffer();

  /**
   * Allows using this buffer again without doing a complete
   * re-initialization
   */
  void clear();

  /**
   * If successful, iovOut/niovOut will on return point to an array of iovs
   * contained within this struct which will contain a serialized
   * representation of the given reply.
   *
   * @return true On success
   */
  bool prepare(McServerRequestContext&& ctx, McReply&& reply,
               mc_protocol_t protocol,
               struct iovec*& iovOut, size_t& niovOut);

 private:
  folly::Optional<McServerRequestContext> ctx_;
  folly::Optional<McReply> reply_;

  /* Write buffers */
  mc_msg_t replyMsg_;
  um_backing_msg_t umMsg_;
  mc_ascii_response_buf_t asciiResponse_;
  static const size_t kMaxIovs = 16;
  size_t niovs_;
  struct iovec iovs_[kMaxIovs];
};

class WriteBufferQueue {
 public:
  WriteBuffer& push() {
    if (freeQueue_.empty()) {
      return queue_.pushBack(folly::make_unique<WriteBuffer>());
    } else {
      return queue_.pushBack(freeQueue_.popFront());
    }
  }

  void pop() {
    auto& wb = freeQueue_.pushBack(queue_.popFront());
    wb.clear();
  }

  bool empty() {
    return queue_.empty();
  }

 private:
  WriteBuffer::Queue queue_;
  WriteBuffer::Queue freeQueue_;
};

}}  // facebook::memcache
