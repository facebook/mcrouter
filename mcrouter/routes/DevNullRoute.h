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

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Same as NullRoute, but with Mcrouter stats reporting.
 */
class DevNullRoute {
 public:
  static std::string routeName() { return "devnull"; }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const { }

  template <class Operation, class Request>
  static typename ReplyType<Operation, Request>::type
  route(const Request& req, Operation) {
    auto& ctx = fiber_local::getSharedCtx();
    stat_incr(ctx->proxy().stats, dev_null_requests_stat, 1);
    using Reply = typename ReplyType<Operation, Request>::type;
    return Reply(DefaultReply, Operation());
  }
};

}}}  // facebook::memcache::mcrouter
