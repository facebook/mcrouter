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

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/routes/AllAsyncRoute.h"

namespace facebook { namespace memcache {

/**
 * Sends the same request to all child route handles.
 * Returns the reply from the first route handle in the list;
 * all other requests complete asynchronously.
 */
template <class RouteHandleIf>
class AllInitialRoute {
 public:
  static std::string routeName() { return "all-initial"; }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*firstChild_, req, Operation());
    asyncRoute_.traverse(req, Operation(), t);
  }

  explicit AllInitialRoute(std::vector<std::shared_ptr<RouteHandleIf>> rh)
      : firstChild_(getFirstAndCheck(rh)),
        asyncRoute_(std::vector<std::shared_ptr<RouteHandleIf>>(
          rh.begin() + 1, rh.end())) {
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    asyncRoute_.route(req, Operation());
    return firstChild_->route(req, Operation());
  }

 private:
  const std::shared_ptr<RouteHandleIf> firstChild_;
  const AllAsyncRoute<RouteHandleIf> asyncRoute_;

  static std::shared_ptr<RouteHandleIf>
  getFirstAndCheck(std::vector<std::shared_ptr<RouteHandleIf>>& rh) {
    assert(rh.size() > 1);
    return std::move(rh[0]);
  }
};

}}
