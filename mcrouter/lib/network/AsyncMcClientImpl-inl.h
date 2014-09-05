/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "folly/Memory.h"
#include "folly/MoveWrapper.h"

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"

namespace facebook { namespace memcache {

template <int Op>
void AsyncMcClientImpl::send(const McRequest& request, McOperation<Op>,
    std::function<void(McReply&&)> callback) {
  DestructorGuard dg(this);

  auto selfPtr = selfPtr_.lock();
  // shouldn't happen.
  assert(selfPtr);

  if (maxPending_ != 0 && getPendingRequestCount() >= maxPending_) {
    callback(McReply(mc_res_local_error));
    return;
  }

  auto op = (mc_op_t)Op;
  auto req = folly::make_unique<ReqInfo>(request, nextMsgId_, op,
                                         connectionOptions_.protocol,
                                         std::move(callback), selfPtr);

  switch (req->reqContext.serializationResult()) {
    case McSerializedRequest::Result::OK:
      incMsgId(nextMsgId_);

      if (outOfOrder_) {
        idMap_[req->id] = req.get();
      }
      sendQueue_.pushBack(std::move(req));
      scheduleNextWriterLoop();
      if (connectionState_ == ConnectionState::DOWN) {
        attemptConnection();
      }
      return;
    case McSerializedRequest::Result::BAD_KEY:
      reply(std::move(req), McReply(mc_res_bad_key));
      return;
    case McSerializedRequest::Result::ERROR:
      reply(std::move(req), McReply(mc_res_local_error));
      return;
  }
}

}} // facebook::memcache
