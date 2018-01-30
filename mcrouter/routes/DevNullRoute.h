/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/ProxyRequestContextTyped.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/DevNullRoute.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

/**
 * Same as NullRoute, but with Mcrouter stats reporting.
 */
template <class RouterInfo>
class DevNullRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;

 public:
  static std::string routeName() {
    return "devnull";
  }

  template <class Request>
  void traverse(const Request&, const RouteHandleTraverser<RouteHandleIf>&)
      const {}

  template <class Request>
  static ReplyT<Request> route(const Request& req) {
    auto& ctx = fiber_local<RouterInfo>::getSharedCtx();
    ctx->proxy().stats().increment(dev_null_requests_stat);
    return createReply(DefaultReply, req);
  }
};

namespace detail {

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeDevNullRoute() {
  return makeRouteHandleWithInfo<RouterInfo, DevNullRoute>();
}

} // detail

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeDevNullRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>&,
    const folly::dynamic&) {
  return detail::makeDevNullRoute<RouterInfo>();
}
} // mcrouter
} // memcache
} // facebook
