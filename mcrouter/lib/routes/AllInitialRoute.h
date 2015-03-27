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
#include <folly/Memory.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/routes/AllAsyncRoute.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache {

/**
 * Sends the same request to all child route handles.
 * Returns the reply from the first route handle in the list;
 * all other requests complete asynchronously.
 */
template <class RouteHandleIf>
class AllInitialRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;
  using StackContext = typename RouteHandleIf::StackContext;

  static std::string routeName() { return "all-initial"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    std::vector<std::shared_ptr<RouteHandleIf>> children;

    if (firstChild_) {
      children.push_back(firstChild_);
    }

    if (asyncRoute_) {
      auto asyncChildren = asyncRoute_->couldRouteTo(req, Operation(), ctx);
      children.insert(children.end(),
          asyncChildren.begin(), asyncChildren.end());
    }

    return children;
  }

  explicit AllInitialRoute(std::vector<std::shared_ptr<RouteHandleIf>> rh) {
    if (rh.empty()) {
      return;
    }

    firstChild_ = *rh.begin();

    if (rh.size() > 1) {
      asyncRoute_ = folly::make_unique<AllAsyncRoute<RouteHandleIf>>(
        std::vector<std::shared_ptr<RouteHandleIf>>(rh.begin() + 1,
                                                    rh.end()));
    }
  }

  AllInitialRoute(RouteHandleFactory<RouteHandleIf>& factory,
                  const folly::dynamic& json) {
    std::vector<std::shared_ptr<RouteHandleIf>> rh;
    if (json.isObject()) {
      if (json.count("children")) {
        rh = factory.createList(json["children"]);
      }
    } else {
      rh = factory.createList(json);
    }

    if (!rh.empty()) {
      firstChild_ = rh[0];
    }

    if (rh.size() > 1) {
      asyncRoute_ = folly::make_unique<AllAsyncRoute<RouteHandleIf>>(
        std::vector<std::shared_ptr<RouteHandleIf>>(rh.begin() + 1,
                                                    rh.end()));
    }
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx,
    StackContext&& sctx) const {

    // no children at all
    if (!firstChild_) {
      return NullRoute<RouteHandleIf>::route(req, Operation(), ctx,
                                             std::move(sctx));
    }

    /* Process all children except first asynchronously */
    if (asyncRoute_) {
      asyncRoute_->route(req, Operation(), ctx, StackContext(sctx));
    }

    return firstChild_->route(req, Operation(), ctx, std::move(sctx));
  }

 private:
  std::shared_ptr<RouteHandleIf> firstChild_;
  std::unique_ptr<AllAsyncRoute<RouteHandleIf>> asyncRoute_;
};

}}
