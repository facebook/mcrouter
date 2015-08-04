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

#include <folly/experimental/fibers/FiberManager.h>

#include "mcrouter/lib/RouteHandleTraverser.h"
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

  explicit AllAsyncRoute(std::vector<std::shared_ptr<RouteHandleIf>> rh)
      : children_(std::move(rh)) {
    assert(!children_.empty());
  }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(children_, req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    auto reqCopy = std::make_shared<Request>(req.clone());
    for (auto& rh : children_) {
      folly::fibers::addTask(
        [rh, reqCopy]() {
          rh->route(*reqCopy, Operation());
        });
    }
    return NullRoute<RouteHandleIf>::route(req, Operation());
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> children_;
};

}}
