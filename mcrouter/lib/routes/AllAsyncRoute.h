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
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache {

/**
 * Sends the same request to all child route handles.
 * Does not wait for response.
 */
template <class RouteHandleIf>
class AllAsyncRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;

  static std::string routeName() { return "all-async"; }

  explicit AllAsyncRoute(std::vector<std::shared_ptr<RouteHandleIf>> rh)
      : children_(std::move(rh)) {
  }

  AllAsyncRoute(RouteHandleFactory<RouteHandleIf>& factory,
                const folly::dynamic& json) {
    if (json.isObject()) {
      if (json.count("children")) {
        children_ = factory.createList(json["children"]);
      }
    } else {
      children_ = factory.createList(json);
    }
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return children_;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx) const {

    if (!children_.empty()) {
      auto reqCopy = std::make_shared<Request>(req.clone());
      for (auto& rh : children_) {
        fiber::addTask(
          [rh, reqCopy, ctx]() {
            rh->route(*reqCopy, Operation(), ctx);
          });
      }
    }
    return NullRoute<RouteHandleIf>::route(req, Operation(), ctx);
  }

 private:
  std::vector<std::shared_ptr<RouteHandleIf>> children_;
};

}}
