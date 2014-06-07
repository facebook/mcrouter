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

#include "folly/dynamic.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache {

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

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    return targets_;
  }

  explicit MissFailoverRoute(
    std::vector<std::shared_ptr<RouteHandleIf>> targets)
      : targets_(std::move(targets)) {
  }

  MissFailoverRoute(RouteHandleFactory<RouteHandleIf>& factory,
                    const folly::dynamic& json) {
    if (json.isObject()) {
      if (json.count("children")) {
        targets_ = factory.createList(json["children"]);
      }
    } else {
      targets_ = factory.createList(json);
    }
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const Request& req, Operation) const {

    if (targets_.empty()) {
      return NullRoute<RouteHandleIf>::route(req, Operation());
    }

    for (size_t i = 0; i < targets_.size() - 1; ++i) {
      auto reply = targets_[i]->route(req, Operation());
      if (reply.isHit()) {
        return reply;
      }
    }

    return targets_.back()->route(req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, typename GetLike<Operation>::Type = 0)
    const {
    return routeImpl(req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, typename DeleteLike<Operation>::Type = 0)
    const {
    return routeImpl(req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    OtherThanT(Operation, GetLike<>, DeleteLike<>) = 0)
    const {

    if (targets_.empty()) {
      return NullRoute<RouteHandleIf>::route(req, Operation());
    }
    return targets_[0]->route(req, Operation());
  }

 private:
  std::vector<std::shared_ptr<RouteHandleIf>> targets_;
};

}}
