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
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyMcReply.h"
#include "mcrouter/ProxyMcRequest.h"
#include "mcrouter/ProxyRequestContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Same as NullRoute, but with Mcrouter stats reporting.
 */
template <class RouteHandleIf>
class DevNullRoute {
 public:
  static std::string routeName() { return "devnull"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    return {};
  }

  template <class Operation>
  static ProxyMcReply route(const ProxyMcRequest& req, Operation) {

    stat_incr(req.context().proxy().stats,
              dev_null_requests_stat, 1);
    return NullRoute<RouteHandleIf>::route(req, Operation());
  }

  template <class Operation, class Request>
  static typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) {

    return NullRoute<RouteHandleIf>::route(req, Operation());
  }
};

}}}  // facebook::memcache::mcrouter
