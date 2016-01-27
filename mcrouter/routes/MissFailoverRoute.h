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

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * For get-like requests, sends the same request sequentially
 * to each destination in the list in order until the first hit reply.
 * If all replies result in errors/misses, returns the reply from the
 * last destination in the list.
 */
template <class RouteHandleIf>
class MissFailoverRoute {
 public:
  static std::string routeName() { return "miss-failover"; }

  template <class Request>
  void traverse(const Request& req,
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
    if (reply.isHit()) {
      return reply;
    }

    // Failover
    return fiber_local::runWithLocals([this, &req]() {
      fiber_local::addRequestClass(RequestClass::kFailover);
      for (size_t i = 1; i < targets_.size() - 1; ++i) {
        auto failoverReply = targets_[i]->route(req);
        if (failoverReply.isHit()) {
          return failoverReply;
        }
      }
      return targets_.back()->route(req);
    });
  }

  template <class Request>
  ReplyT<Request> route(const Request& req, GetLikeT<Request> = 0) const {
    return routeImpl(req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req, DeleteLikeT<Request> = 0) const {
    return routeImpl(req);
  }

  template <class Request>
  ReplyT<Request> route(
      const Request& req,
      OtherThanT<Request, GetLike<>, DeleteLike<>> = 0) const {

    return targets_[0]->route(req);
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> targets_;
};

}}} // facebook::memcache::mcrouter
