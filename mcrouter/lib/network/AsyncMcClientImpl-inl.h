/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Memory.h>

#include "mcrouter/lib/network/FBTrace.h"
#include "mcrouter/lib/network/ReplyStatsContext.h"

namespace facebook {
namespace memcache {

template <class Request>
ReplyT<Request> AsyncMcClientImpl::sendSync(
    const Request& request,
    std::chrono::milliseconds timeout,
    ReplyStatsContext* replyContext) {
  auto selfPtr = selfPtr_.lock();
  // shouldn't happen.
  assert(selfPtr);
  assert(folly::fibers::onFiber());

  if (maxPending_ != 0 && getPendingRequestCount() >= maxPending_) {
    return ReplyT<Request>(mc_res_local_error);
  }

  // We need to send fbtrace before serializing, or otherwise we are going to
  // miss fbtrace id.
  fbTraceOnSend(request, *connectionOptions_.accessPoint);

  McClientRequestContext<Request> ctx(
      request,
      nextMsgId_,
      connectionOptions_.accessPoint->getProtocol(),
      std::move(selfPtr),
      queue_,
      [](ParserT& parser) { parser.expectNext<Request>(); },
      requestStatusCallbacks_.onStateChange,
      supportedCompressionCodecs_);
  sendCommon(ctx);

  // Wait for the reply.
  auto reply = ctx.waitForReply(timeout);

  if (replyContext) {
    *replyContext = ctx.getReplyStatsContext();
  }

  // Schedule next writer loop, in case we didn't before
  // due to max inflight requests limit.
  scheduleNextWriterLoop();

  return reply;
}

template <class Reply>
void AsyncMcClientImpl::replyReady(
    Reply&& r,
    uint64_t reqId,
    ReplyStatsContext replyStatsContext) {
  assert(connectionState_ == ConnectionState::UP);
  DestructorGuard dg(this);

  queue_.reply(reqId, std::move(r), replyStatsContext);
}

template <class Request>
double AsyncMcClientImpl::getDropProbability() const {
  return 0.0;
}

template <>
inline double AsyncMcClientImpl::getDropProbability<McSetRequest>() const {
  return parser_ ? parser_->getDropProbability() : 0.0;
}

template <>
inline double AsyncMcClientImpl::getDropProbability<McGetRequest>() const {
  return parser_ ? parser_->getDropProbability() : 0.0;
}

template <>
inline double AsyncMcClientImpl::getDropProbability<McDeleteRequest>() const {
  return parser_ ? parser_->getDropProbability() : 0.0;
}
}
} // facebook::memcache
