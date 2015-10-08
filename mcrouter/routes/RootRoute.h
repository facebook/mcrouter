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

#include <memory>
#include <string>
#include <vector>

#include <folly/Likely.h>
#include <folly/experimental/fibers/FiberManager.h>

#include "mcrouter/config.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/RouteHandleMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

class RootRoute {
 public:
  static std::string routeName() { return "root"; }

  RootRoute(proxy_t* proxy, const RouteSelectorMap& routeSelectors)
      : opts_(proxy->getRouterOptions()),
        rhMap_(routeSelectors,
               opts_.default_route,
               opts_.send_invalid_route_to_default) {}

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    const auto* rhPtr =
      rhMap_.getTargetsForKeyFast(req.routingPrefix(), req.routingKey());
    if (LIKELY(rhPtr != nullptr)) {
      for (const auto& rh : *rhPtr) {
        t(*rh, req, Operation());
      }
      return;
    }
    auto v = rhMap_.getTargetsForKeySlow(req.routingPrefix(), req.routingKey());
    for (const auto& rh : v) {
      t(*rh, req, Operation());
    }
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    ReplyT<Operation, Request> reply;
    /* If we have to send to more than one prefix,
       wait for the first in the list to reply and let others
       run in the background.

       This is a good default for /star/star/ requests. */
    const auto* rhPtr =
      rhMap_.getTargetsForKeyFast(req.routingPrefix(), req.routingKey());
    if (UNLIKELY(rhPtr == nullptr)) {
      auto rh = rhMap_.getTargetsForKeySlow(req.routingPrefix(),
                                            req.routingKey());
      reply = routeImpl(rh, req, Operation());
    } else {
      reply = routeImpl(*rhPtr, req, Operation());
    }

    if (reply.isError() && opts_.group_remote_errors) {
      reply = ReplyT<Operation, Request>(mc_res_remote_error);
    }

    return reply;
  }

 private:
  const McrouterOptions& opts_;
  RouteHandleMap rhMap_;

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation,
    typename GetLike<Operation>::Type = 0) const {

    auto reply = doRoute(rh, req, Operation());
    if (reply.isError() && opts_.miss_on_get_errors && !rh.empty()) {
      /* rh.empty() case: for backwards compatibility,
         always surface invalid routing errors */
      reply = ReplyT<Operation, Request>(DefaultReply, Operation());
    }
    return reply;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation,
    typename ArithmeticLike<Operation>::Type = 0)
    const {

    ReplyT<Operation, Request> reply(DefaultReply, Operation());

    if (!opts_.allow_only_gets) {
      reply = doRoute(rh, req, Operation());
      if (reply.isError()) {
        reply = ReplyT<Operation, Request>(DefaultReply, Operation());
      }
    }
    return reply;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation,
    OtherThanT(Operation, GetLike<>, ArithmeticLike<>) = 0)
    const {

    ReplyT<Operation, Request> reply(DefaultReply, Operation());

    if (!opts_.allow_only_gets) {
      reply = doRoute(rh, req, Operation());
    }

    return reply;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type doRoute(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation) const {

    ReplyT<Operation, Request> reply(ErrorReply);

    if (!rh.empty()) {
      if (rh.size() > 1) {
        auto reqCopy = std::make_shared<Request>(req.clone());
        for (size_t i = 1; i < rh.size(); ++i) {
          auto r = rh[i];
          folly::fibers::addTask(
            [r, reqCopy]() {
              r->route(*reqCopy, Operation());
            });
        }
      }
      reply = rh[0]->route(req, Operation());
    }
    return reply;
  }
};

}}}  // facebook::memcache::mcrouter
