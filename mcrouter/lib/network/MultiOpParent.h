/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Optional.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/network/McServerRequestContext.h"

namespace facebook { namespace memcache {

class McServerSession;

/**
 * We use this struct to track the state of multi-op operations
 * for in-order protocols.
 *
 * Consider the request "get a b". This generates four contexts:
 *   block_ctx, a_ctx, b_ctx, end_ctx
 *
 * The purpose of the block_ctx is to prevent any writes until
 * both a and b complete (it's a hack that uses the head of line
 * blocking property of the in-order protocol).
 *
 * a_ctx and b_ctx are the real contexts dispatched to the application,
 * and they both have back pointers to this parent struct.
 *
 * end_ctx is for writing the END message on the wire.
 *
 * Error handling: if either a or b returns an error, we want to
 * turn the entire reply into a single error reply.
 * How it's implemented: if b_ctx returns an error, we'll store the error
 * reply in the parent. Then when unblocked, both a_ctx and b_ctx
 * will check that the parent has a stored error and will not
 * write anything to the transport (same as if 'noreply' was set).
 *
 * Finally the end context will write out the stored error reply.
 */
class MultiOpParent {
 public:
  MultiOpParent(McServerSession& session, uint64_t blockReqid);

  /**
   * Examine a reply of one of the sub-requests, and steal it if
   * it's an error reply.
   * @return true if the reply was moved (stolen) by this parent
   */
  bool reply(McReply&& r);

  /**
   * Notify that a sub request is waiting for a reply.
   */
  void recordRequest() {
    ++waiting_;
  }

  /**
   * Notify that we saw an mc_op_end, and create the 'end' context
   * with this id.
   */
  void recordEnd(uint64_t reqid);

  /**
   * @return true if an error was observed
   */
  bool error() const {
    return error_;
  }

 private:
  size_t waiting_{0};
  folly::Optional<McReply> reply_;
  bool error_{false};

  McServerSession& session_;
  McServerRequestContext block_;
  folly::Optional<McServerRequestContext> end_;

  void release();
};

}}  // facebook::memcache
