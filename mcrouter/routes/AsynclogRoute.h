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
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Async logs a failed request. It assumes the required data is available in
 * the reply.
 */
class AsynclogRoute {
 public:
  std::string routeName() const { return "asynclog:" + asynclogName_; }

  AsynclogRoute(McrouterRouteHandlePtr rh, std::string asynclogName)
      : rh_(std::move(rh)),
        asynclogName_(std::move(asynclogName)) {
  }

  template <class Operation, class Request>
  std::vector<McrouterRouteHandlePtr> couldRouteTo(
    const Request& req, Operation) const {

    return {rh_};
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    typename DeleteLike<Operation>::Type = 0) const {

    auto reply = rh_->route(req, Operation());
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

    auto proxy = &fiber_local::getSharedCtx()->proxy();
    folly::fibers::Baton b;
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
    using Reply = typename ReplyType<Operation, Request>::type;
    return Reply(DefaultReply, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    OtherThanT(Operation, DeleteLike<>) = 0) const {

    return rh_->route(req, Operation());
  }
 private:
  const McrouterRouteHandlePtr rh_;
  const std::string asynclogName_;
};

}}}  // facebook::memcache::mcrouter
