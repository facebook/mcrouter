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

#include <memory>
#include <string>
#include <vector>

#include <folly/Likely.h>
#include <folly/fibers/FiberManager.h>

#include "mcrouter/ProxyBase.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/carbon/RequestReplyUtil.h"
#include "mcrouter/lib/carbon/RoutingGroups.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/routes/RouteHandleMap.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouteHandleIf>
class RootRoute {
 public:
  static std::string routeName() {
    return "root";
  }

  RootRoute(
      ProxyBase& proxy,
      const RouteSelectorMap<RouteHandleIf>& routeSelectors)
      : opts_(proxy.getRouterOptions()),
        rhMap_(
            routeSelectors,
            opts_.default_route,
            opts_.send_invalid_route_to_default) {}

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    const auto* rhPtr = rhMap_.getTargetsForKeyFast(
        req.key().routingPrefix(), req.key().routingKey());
    if (LIKELY(rhPtr != nullptr)) {
      for (const auto& rh : *rhPtr) {
        t(*rh, req);
      }
      return;
    }
    auto v = rhMap_.getTargetsForKeySlow(
        req.key().routingPrefix(), req.key().routingKey());
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
    const auto* rhPtr = rhMap_.getTargetsForKeyFast(
        req.key().routingPrefix(), req.key().routingKey());

    auto reply = UNLIKELY(rhPtr == nullptr)
        ? routeImpl(
              rhMap_.getTargetsForKeySlow(
                  req.key().routingPrefix(), req.key().routingKey()),
              req)
        : routeImpl(*rhPtr, req);

    if (isErrorResult(reply.result()) && opts_.group_remote_errors) {
      reply = ReplyT<Request>(mc_res_remote_error);
    }

    return reply;
  }

 private:
  const McrouterOptions& opts_;
  RouteHandleMap<RouteHandleIf> rhMap_;

  template <class Request>
  ReplyT<Request> routeImpl(
      const std::vector<std::shared_ptr<RouteHandleIf>>& rh,
      const Request& req,
      carbon::GetLikeT<Request> = 0) const {
    auto reply = doRoute(rh, req);
    if (isErrorResult(reply.result()) && opts_.miss_on_get_errors &&
        !rh.empty()) {
      /* rh.empty() case: for backwards compatibility,
         always surface invalid routing errors */
      auto originalResult = reply.result();
      reply = createReply(DefaultReply, req);
      carbon::setMessageIfPresent(
          reply,
          folly::to<std::string>(
              "Error reply transformed into miss due to miss_on_get_errors. "
              "Original reply result: ",
              mc_res_to_string(originalResult)));
    }
    return reply;
  }

  template <class Request>
  ReplyT<Request> routeImpl(
      const std::vector<std::shared_ptr<RouteHandleIf>>& rh,
      const Request& req,
      carbon::ArithmeticLikeT<Request> = 0) const {
    auto reply = opts_.allow_only_gets ? createReply(DefaultReply, req)
                                       : doRoute(rh, req);
    if (isErrorResult(reply.result())) {
      reply = createReply(DefaultReply, req);
    }
    return reply;
  }

  template <class Request>
  ReplyT<Request> routeImpl(
      const std::vector<std::shared_ptr<RouteHandleIf>>& rh,
      const Request& req,
      carbon::OtherThanT<Request, carbon::GetLike<>, carbon::ArithmeticLike<>> =
          0) const {
    if (!opts_.allow_only_gets) {
      return doRoute(rh, req);
    }

    return createReply(DefaultReply, req);
  }

  template <class Request>
  ReplyT<Request> doRoute(
      const std::vector<std::shared_ptr<RouteHandleIf>>& rh,
      const Request& req) const {
    if (!rh.empty()) {
      if (rh.size() > 1) {
        auto reqCopy = std::make_shared<Request>(req);
        for (size_t i = 1; i < rh.size(); ++i) {
          auto r = rh[i];
          folly::fibers::addTask([r, reqCopy]() { r->route(*reqCopy); });
        }
      }
      return rh[0]->route(req);
    }
    return createReply<Request>(ErrorReply);
  }
};
}
}
} // facebook::memcache::mcrouter
