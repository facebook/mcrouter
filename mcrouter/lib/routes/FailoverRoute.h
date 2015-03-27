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

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache {

/**
 * Sends the same request sequentially to each destination in the list in order,
 * until the first non-error reply.  If all replies result in errors, returns
 * the last destination's reply.
 */
template <class RouteHandleIf>
class FailoverRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;

  static std::string routeName() { return "failover"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return targets_;
  }

  FailoverRoute() = default;

  explicit FailoverRoute(std::vector<std::shared_ptr<RouteHandleIf>> targets)
      : targets_(std::move(targets)) {
  }

  FailoverRoute(RouteHandleFactory<RouteHandleIf>& factory,
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
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx) const {

    if (targets_.empty()) {
      return NullRoute<RouteHandleIf>::route(req, Operation(), ctx);
    }

    for (size_t i = 0; i + 1 < targets_.size(); ++i) {
      auto reply = targets_[i]->route(req, Operation(), ctx);
      if (!reply.isFailoverError()) {
        return reply;
      }
    }

    return targets_.back()->route(req, Operation(), ctx);
  }

 private:
  std::vector<std::shared_ptr<RouteHandleIf>> targets_;
};

}}
