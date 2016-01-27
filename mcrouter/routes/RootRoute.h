/*
 *  Copyright (c) 2016, Facebook, Inc.
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

#include <folly/experimental/fibers/FiberManager.h>
#include <folly/Likely.h>

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

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    const auto* rhPtr =
      rhMap_.getTargetsForKeyFast(req.routingPrefix(), req.routingKey());
    if (LIKELY(rhPtr != nullptr)) {
      for (const auto& rh : *rhPtr) {
        t(*rh, req);
      }
      return;
    }
    auto v = rhMap_.getTargetsForKeySlow(req.routingPrefix(), req.routingKey());
    for (const auto& rh : v) {
      t(*rh, req);
    }
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    /* If we have to send to more than one prefix,
       wait for the first in the list to reply and let others
       run in the background.

       This is a good default for /star/star/ requests. */
    const auto* rhPtr =
      rhMap_.getTargetsForKeyFast(req.routingPrefix(), req.routingKey());

    auto reply = UNLIKELY(rhPtr == nullptr)
                     ? routeImpl(rhMap_.getTargetsForKeySlow(
                                     req.routingPrefix(), req.routingKey()),
                                 req)
                     : routeImpl(*rhPtr, req);

    if (reply.isError() && opts_.group_remote_errors) {
      reply = ReplyT<Request>(mc_res_remote_error);
    }

    return reply;
  }

 private:
  const McrouterOptions& opts_;
  RouteHandleMap rhMap_;

  template <class Request>
  ReplyT<Request> routeImpl(const std::vector<McrouterRouteHandlePtr>& rh,
                            const Request& req,
                            GetLikeT<Request> = 0) const {

    auto reply = doRoute(rh, req);
    if (reply.isError() && opts_.miss_on_get_errors && !rh.empty()) {
      /* rh.empty() case: for backwards compatibility,
         always surface invalid routing errors */
      reply = ReplyT<Request>(DefaultReply, req);
    }
    return reply;
  }

  template <class Request>
  ReplyT<Request> routeImpl(const std::vector<McrouterRouteHandlePtr>& rh,
                            const Request& req,
                            ArithmeticLikeT<Request> = 0) const {

    auto reply = opts_.allow_only_gets
                     ? ReplyT<Request>(DefaultReply, req)
                     : doRoute(rh, req);
    if (reply.isError()) {
      reply = ReplyT<Request>(DefaultReply, req);
    }
    return reply;
  }

  template <class Request>
  ReplyT<Request> routeImpl(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req,
    OtherThanT<Request, GetLike<>, ArithmeticLike<>> = 0) const {

    if (!opts_.allow_only_gets) {
      return doRoute(rh, req);
    }

    return ReplyT<Request>(DefaultReply, req);
  }

  template <class Request>
  ReplyT<Request> doRoute(const std::vector<McrouterRouteHandlePtr>& rh,
                          const Request& req) const {

    if (!rh.empty()) {
      if (rh.size() > 1) {
        auto reqCopy = std::make_shared<Request>(req.clone());
        for (size_t i = 1; i < rh.size(); ++i) {
          auto r = rh[i];
          folly::fibers::addTask(
            [r, reqCopy]() {
              r->route(*reqCopy);
            });
        }
      }
      return rh[0]->route(req);
    }
    return ReplyT<Request>(ErrorReply);
  }
};

}}}  // facebook::memcache::mcrouter
