/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Memory.h>

#include "mcrouter/lib/cycles/Cycles.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"

#include "mcrouter/lib/network/FBTrace.h"

namespace facebook { namespace memcache {

template <class Operation, class Request>
typename ReplyType<Operation, Request>::type
AsyncMcClientImpl::sendSync(const Request& request, Operation,
                            std::chrono::milliseconds timeout) {
  auto selfPtr = selfPtr_.lock();
  // shouldn't happen.
  assert(selfPtr);
  assert(folly::fibers::onFiber());

  using Reply = typename ReplyType<Operation, Request>::type;

  if (maxPending_ != 0 && getPendingRequestCount() >= maxPending_) {
    return Reply(mc_res_local_error);
  }

  // We need to send fbtrace before serializing, or otherwise we are going to
  // miss fbtrace id.
  fbTraceOnSend(Operation(), request, *connectionOptions_.accessPoint);

  McClientRequestContext<Operation, Request> ctx(
      request,
      nextMsgId_,
      connectionOptions_.accessPoint->getProtocol(),
      std::move(selfPtr),
      queue_,
      [](ParserT& parser) { parser.expectNext<Operation, Request>(); },
      connectionOptions_.useTyped);
  sendCommon(ctx);

  // Wait for the reply.
  return ctx.waitForReply(timeout);
}

template <class Reply>
void AsyncMcClientImpl::replyReady(Reply&& r, uint64_t reqId) {
  assert(connectionState_ == ConnectionState::UP);
  DestructorGuard dg(this);

  queue_.reply(reqId, std::move(r));
}

}} // facebook::memcache
