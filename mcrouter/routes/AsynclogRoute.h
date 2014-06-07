/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyRequest.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/proxy.h"
#include "mcrouter/route.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Callback that will be called on delete errors.
 *
 * @param preq The proxy request
 * @param pclient The proxy client we tried sending to
 * @param req The raw request we tried to send out.
 */
typedef std::function<void(proxy_request_t*,
                           std::shared_ptr<const ProxyClientCommon>,
                           const mc_msg_t*,
                           folly::StringPiece)> AsynclogFunc;

/**
 * Async logs a failed request. It assumes the required data is available in
 * the reply.
 */
template <class RouteHandleIf>
class AsynclogRoute {
 public:
  static std::string routeName() { return "asynclog"; }

  AsynclogRoute(std::shared_ptr<RouteHandleIf> rh,
                std::string poolName,
                AsynclogFunc asyncLog)
      : rh_(std::move(rh)),
        poolName_(std::move(poolName)),
        asyncLog_(std::move(asyncLog)) {
    assert(asyncLog_ != nullptr);
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    return {rh_};
  }

  template <class Operation>
  ProxyMcReply route(
    const ProxyMcRequest& req, Operation,
    typename DeleteLike<Operation>::Type = 0) const {

    auto reply = rh_->route(req, Operation());
    if (!reply.isError()) {
      return reply;
    }
    auto dest = reply.getDestination();
    if (!dest) {
      return reply;
    }
    auto msg = generateMsg(dest, req, Operation());
    auto& asyncLog = asyncLog_;
    auto& poolName = poolName_;
    proxy_request_t* preq = &req.context().proxyRequest();
    fiber::runInMainContext(
      [&asyncLog, preq, dest, &msg, &poolName] () {
        asyncLog(preq, dest, msg.get(), poolName);
      });
    return NullRoute<RouteHandleIf>::route(req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    return rh_->route(req, Operation());
  }

 private:
  const std::shared_ptr<RouteHandleIf> rh_;
  const std::string poolName_;
  AsynclogFunc asyncLog_;

  template <class Request, int M>
  McMsgRef generateMsg(std::shared_ptr<const ProxyClientCommon> dest,
                       const Request& req, McOperation<M>) const {
    if (dest->keep_routing_prefix) {
      return req.dependentMsg((mc_op_t)M);
    }

    return req.dependentMsgStripRoutingPrefix((mc_op_t)M);
  }

};

}}}  // facebook::memcache::mcrouter
