/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <folly/Memory.h>
#include <folly/MoveWrapper.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"

#ifndef LIBMC_FBTRACE_DISABLE
#include "mcrouter/lib/network/FBTrace.h"
#endif

namespace facebook { namespace memcache {

namespace {

template <int Op>
std::function<void(const McReply&)> traceOnSend(const McRequest& request,
                                                McOperation<Op>,
                                                const AccessPoint& ap) {
#ifndef LIBMC_FBTRACE_DISABLE
  if (fbTraceOnSend(McOperation<Op>(), request, ap)) {
    const mc_fbtrace_info_s* traceInfo = request.fbtraceInfo();
    return [traceInfo] (const McReply& reply) {
      fbTraceOnReceive(McOperation<Op>(), traceInfo, reply);
    };
  }
#endif
  return nullptr;
}

} // anonymous namespace

template <int Op>
McReply AsyncMcClientImpl::sendSync(const McRequest& request, McOperation<Op>) {
  auto selfPtr = selfPtr_.lock();
  // shouldn't happen.
  assert(selfPtr);
  assert(fiber::onFiber());

  if (maxPending_ != 0 && getPendingRequestCount() >= maxPending_) {
    return McReply(mc_res_local_error);
  }

  // We need to send fbtrace before serializing, or otherwise we are going to
  // miss fbtrace id.
  std::function<void(const McReply&)> traceCallback =
    traceOnSend(request, McOperation<Op>(), connectionOptions_.accessPoint);

  auto op = (mc_op_t)Op;
  ReqInfo req(request, nextMsgId_, op,
              connectionOptions_.accessPoint.getProtocol(), selfPtr);
  req.traceCallback = traceCallback;
  sendCommon(req.createDummyPtr());

  // We sent request successfully, wait for the result.
  req.syncContext.baton.wait();
  return std::move(req.syncContext.reply);
}

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

  // We need to send fbtrace before serializing, or otherwise we are going to
  // miss fbtrace id.
  std::function<void(const McReply&)> traceCallback =
    traceOnSend(request, McOperation<Op>(), connectionOptions_.accessPoint);

  auto op = (mc_op_t)Op;
  auto req = ReqInfo::getFromPool(request, nextMsgId_, op,
                                  connectionOptions_.accessPoint.getProtocol(),
                                  std::move(callback), selfPtr);
  req->traceCallback = traceCallback;

  sendCommon(std::move(req));
}

}} // facebook::memcache
