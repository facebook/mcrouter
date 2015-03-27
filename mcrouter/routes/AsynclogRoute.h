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
#include "mcrouter/awriter.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/lib/McOperationTraits.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/McrouterLogFailure.h"
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
  using ContextPtr = typename RouteHandleIf::ContextPtr;

  std::string routeName() const { return "asynclog:" + asynclogName_; }

  AsynclogRoute(std::shared_ptr<RouteHandleIf> rh,
                std::string asynclogName)
      : rh_(std::move(rh)),
        asynclogName_(std::move(asynclogName)) {
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return {rh_};
  }

  template <class Operation>
  ProxyMcReply route(
    const ProxyMcRequest& req, Operation, const ContextPtr& ctx,
    typename DeleteLike<Operation>::Type = 0) const {

    auto reply = rh_->route(req, Operation(), ctx);
    if (!reply.isFailoverError()) {
      return reply;
    }
    auto dest = reply.getDestination();
    if (!dest) {
      return reply;
    }
    folly::StringPiece key = dest->keep_routing_prefix ?
      req.fullKey() :
      req.keyWithoutRoute();
    folly::StringPiece asynclogName = asynclogName_;

    auto proxy = &ctx->proxy();
    Baton b;
    auto res = proxy->router->asyncWriter().run(
      [&b, proxy, &dest, key, asynclogName] () {
        asynclog_delete(proxy, dest, key, asynclogName);
        b.post();
      }
    );
    if (!res) {
      logFailure(proxy->router, memcache::failure::Category::kOutOfResources,
                 "Could not enqueue asynclog request (key {}, pool {})",
                 key, asynclogName);
    } else {
      /* Don't reply to the user until we safely logged the request to disk */
      b.wait();
      stat_incr(proxy->stats, asynclog_requests_stat, 1);
    }
    return NullRoute<RouteHandleIf>::route(req, Operation(), ctx);
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return rh_->route(req, Operation(), ctx);
  }

 private:
  const std::shared_ptr<RouteHandleIf> rh_;
  const std::string asynclogName_;
};

}}}  // facebook::memcache::mcrouter
