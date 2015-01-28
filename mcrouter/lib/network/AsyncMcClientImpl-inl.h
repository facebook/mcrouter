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
#include <folly/MoveWrapper.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"

#include "mcrouter/lib/network/FBTrace.h"

namespace facebook { namespace memcache {

template <class Operation, class Request>
typename ReplyType<Operation, Request>::type
AsyncMcClientImpl::sendSync(const Request& request, Operation) {
  auto selfPtr = selfPtr_.lock();
  // shouldn't happen.
  assert(selfPtr);
  assert(fiber::onFiber());

  if (maxPending_ != 0 && getPendingRequestCount() >= maxPending_) {
    return typename ReplyType<Operation, Request>::type(mc_res_local_error);
  }

  // We need to send fbtrace before serializing, or otherwise we are going to
  // miss fbtrace id.
  fbTraceOnSend(Operation(), request, connectionOptions_.accessPoint);

  McClientRequestContextSync<Operation, Request> ctx(
    Operation(), request, nextMsgId_,
    connectionOptions_.accessPoint.getProtocol(), selfPtr);
  sendCommon(ctx.createDummyPtr());

  // We sent request successfully, wait for the result.
  ctx.wait();
  return ctx.getReply();
}

template <class Operation, class Request, class F>
void AsyncMcClientImpl::send(const Request& request, Operation, F&& f) {
  DestructorGuard dg(this);

  auto selfPtr = selfPtr_.lock();
  // shouldn't happen.
  assert(selfPtr);

  if (maxPending_ != 0 && getPendingRequestCount() >= maxPending_) {
    f(typename ReplyType<Operation, Request>::type(mc_res_local_error));
    return;
  }

  // We need to send fbtrace before serializing, or otherwise we are going to
  // miss fbtrace id.
  fbTraceOnSend(Operation(), request, connectionOptions_.accessPoint);

  auto ctx = McClientRequestContextBase::createAsync(
    Operation(), request, std::forward<F>(f), nextMsgId_,
    connectionOptions_.accessPoint.getProtocol(), selfPtr);
  sendCommon(std::move(ctx));
}

template <class Reply>
void AsyncMcClientImpl::reply(McClientRequestContextBase::UniquePtr req,
                              Reply&& r) {
  idMap_.erase(req->id);
  if (!req->reply(std::move(r))) {
    req->replyError(mc_res_local_error);
  }
}

template <class Reply>
void AsyncMcClientImpl::replyReady(Reply&& r, uint64_t reqId) {
  assert(connectionState_ == ConnectionState::UP);
  DestructorGuard dg(this);

  // Local error in ascii protocol means that there was a protocol level error,
  // e.g. we sent some command that server didn't understand. We need to log
  // the original request and close the connection.
  if (r.result() == mc_res_local_error &&
      connectionOptions_.accessPoint.getProtocol() == mc_ascii_protocol) {
    logCriticalAsciiError();
    processShutdown();
    return;
  }

  if (!outOfOrder_) {
    reqId = nextInflightMsgId_;
    incMsgId(nextInflightMsgId_);
  }

  auto ctx = getRequestContext(reqId);

  // We might have already replied this request with an error.
  if (ctx) {
    reply(std::move(ctx), std::move(r));
  }
}

}} // facebook::memcache
