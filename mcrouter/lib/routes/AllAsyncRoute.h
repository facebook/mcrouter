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
  static std::string routeName() { return "all-async"; }

  AllAsyncRoute(std::vector<std::shared_ptr<RouteHandleIf>> rh)
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
    const Request& req, Operation) const {

    return children_;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    for (auto& rh : children_) {
      auto reqCopy = folly::MoveWrapper<Request>(req.clone());
      fiber::addTask(
        [rh, reqCopy]() {
          rh->route(*reqCopy, Operation());
        });
    }
    return NullRoute<RouteHandleIf>::route(req, Operation());
  }

 private:
  std::vector<std::shared_ptr<RouteHandleIf>> children_;
};

}}
