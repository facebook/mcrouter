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

#include "mcrouter/async.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/lib/McOperationTraits.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyMcReply.h"
#include "mcrouter/ProxyMcRequest.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/route.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Async logs a failed request. It assumes the required data is available in
 * the reply.
 */
template <class RouteHandleIf>
class AsynclogRoute {
 public:
  std::string routeName() const { return "asynclog:" + asynclogName_; }

  AsynclogRoute(std::shared_ptr<RouteHandleIf> rh,
                std::string asynclogName)
      : rh_(std::move(rh)),
        asynclogName_(std::move(asynclogName)) {
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
    if (!reply.isFailoverError()) {
      return reply;
    }
    auto dest = reply.getDestination();
    if (!dest) {
      return reply;
    }
    auto msg = generateMsg(dest, req, Operation());
    auto& asynclogName = asynclogName_;
    proxy_request_t* preq = &req.context().ctx().proxyRequest();
    fiber::runInMainContext(
      [preq, dest, &msg, &asynclogName] () {
        asynclog_command(preq, dest, msg.get(), asynclogName);
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
  const std::string asynclogName_;

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
