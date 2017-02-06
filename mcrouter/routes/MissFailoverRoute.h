/*
 *  Copyright (c) 2017, Facebook, Inc.
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

#include <folly/dynamic.h>

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/network/CarbonMessageTraits.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

/**
 * For get-like requests, sends the same request sequentially
 * to each destination in the list in order until the first hit reply.
 * If all replies result in errors/misses, returns the reply from the
 * last destination in the list.
 */
template <class RouterInfo>
class MissFailoverRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;

 public:
  static std::string routeName() {
    return "miss-failover";
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(targets_, req);
  }

  explicit MissFailoverRoute(
      std::vector<std::shared_ptr<RouteHandleIf>> targets)
      : targets_(std::move(targets)) {
    assert(targets_.size() > 1);
  }

  template <class Request>
  ReplyT<Request> routeImpl(const Request& req) const {
    auto reply = targets_[0]->route(req);
    if (isHitResult(reply.result())) {
      return reply;
    }

    // Failover
    return fiber_local<RouterInfo>::runWithLocals([this, &req]() {
      fiber_local<RouterInfo>::addRequestClass(RequestClass::kFailover);
      for (size_t i = 1; i < targets_.size() - 1; ++i) {
        auto failoverReply = targets_[i]->route(req);
        if (isHitResult(failoverReply.result())) {
          return failoverReply;
        }
      }
      return targets_.back()->route(req);
    });
  }

  template <class Request>
  ReplyT<Request> route(const Request& req, carbon::GetLikeT<Request> = 0)
      const {
    return routeImpl(req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req, carbon::DeleteLikeT<Request> = 0)
      const {
    return routeImpl(req);
  }

  template <class Request>
  ReplyT<Request> route(
      const Request& req,
      carbon::OtherThanT<Request, carbon::GetLike<>, carbon::DeleteLike<>> =
          0) const {
    return targets_[0]->route(req);
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> targets_;
};

namespace detail {

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeMissFailoverRoute(
    std::vector<typename RouterInfo::RouteHandlePtr> targets) {
  if (targets.empty()) {
    return createNullRoute<typename RouterInfo::RouteHandleIf>();
  }

  if (targets.size() == 1) {
    return std::move(targets[0]);
  }

  return makeRouteHandleWithInfo<RouterInfo, MissFailoverRoute>(
      std::move(targets));
}

} // detail

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeMissFailoverRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  std::vector<typename RouterInfo::RouteHandlePtr> children;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
  } else {
    children = factory.createList(json);
  }
  return detail::makeMissFailoverRoute<RouterInfo>(std::move(children));
}
} // mcrouter
} // memcache
} // facebook
