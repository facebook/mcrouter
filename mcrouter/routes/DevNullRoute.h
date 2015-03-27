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

#include "mcrouter/config-impl.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/proxy.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Same as NullRoute, but with Mcrouter stats reporting.
 */
template <class RouteHandleIf>
class DevNullRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;
  using StackContext = typename RouteHandleIf::StackContext;

  static std::string routeName() { return "devnull"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return {};
  }

  template <class Operation>
  static McReply route(const McRequest& req, Operation, const ContextPtr& ctx,
                       StackContext&& sctx) {
    stat_incr(ctx->proxy().stats, dev_null_requests_stat, 1);
    return NullRoute<RouteHandleIf>::route(req, Operation(), ctx,
                                           std::move(sctx));
  }

  template <class Operation, class Request>
  static typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx, StackContext&& sctx) {

    return NullRoute<RouteHandleIf>::route(req, Operation(), ctx,
                                           std::move(sctx));
  }
};

}}}  // facebook::memcache::mcrouter
