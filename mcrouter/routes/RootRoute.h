/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mcrouter/config.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/routes/ErrorRoute.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/RouteHandleMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

class RootRoute {
 public:
  static std::string routeName() { return "root"; }

  RootRoute(proxy_t* proxy, const RouteSelectorMap& routeSelectors)
      : opts_(proxy->opts),
        rhMap_(routeSelectors, proxy->opts.default_route,
               proxy->opts.send_invalid_route_to_default) {
  }

  template <class Operation, class Request>
  std::vector<McrouterRouteHandlePtr> couldRouteTo(
    const Request& req, Operation) const {

    return rhMap_.getTargetsForKey(req.routingPrefix(), req.routingKey());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {
    /* If we have to send to more than one prefix,
       wait for the first in the list to reply and let others
       run in the background.

       This is a good default for /star/star/ requests. */
    const auto& rh =
      rhMap_.getTargetsForKey(req.routingPrefix(), req.routingKey());
    return routeImpl(rh, req, Operation());
  }

 private:
  const McrouterOptions& opts_;
  RouteHandleMap rhMap_;

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation, typename GetLike<Operation>::Type = 0)
    const {

    auto reply = doRoute(rh, req, Operation());
    if (!reply.isError() || rh.empty()) {
      /* rh.empty() case: for backwards compatibility,
         always surface invalid routing errors */
      return reply;
    }

    if (!preprocessGetErrors(opts_, reply) && opts_.miss_on_get_errors) {
      reply = NullRoute<McrouterRouteHandleIf>::route(req, Operation());
    }

    return reply;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation, typename ArithmeticLike<Operation>::Type = 0)
    const {

    auto reply = doRoute(rh, req, Operation());
    if (reply.isError()) {
      return NullRoute<McrouterRouteHandleIf>::route(req, Operation());
    }
    return reply;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation,
    OtherThanT(Operation, GetLike<>, ArithmeticLike<>) = 0)
    const {

    return doRoute(rh, req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type doRoute(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation) const {

    if (rh.empty()) {
      return ErrorRoute<McrouterRouteHandleIf>().route(req, Operation());
    }

    if (rh.size() > 1) {
      auto reqCopy = std::make_shared<Request>(req.clone());
      for (size_t i = 1; i < rh.size(); ++i) {
        auto r = rh[i];
        fiber::addTask(
          [r, reqCopy]() {
            r->route(*reqCopy, Operation());
          });
      }
    }
    return rh[0]->route(req, Operation());
  }
};

}}}  // facebook::memcache::mcrouter
