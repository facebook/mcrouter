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

#include "mcrouter/lib/McOperationTraits.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Async logs a failed request. It assumes the required data is available in
 * the reply.
 */
class AsynclogRoute {
 public:
  std::string routeName() const { return "asynclog:" + asynclogName_; }

  AsynclogRoute(McrouterRouteHandlePtr rh, std::string asynclogName)
      : rh_(std::move(rh)),
        asynclogName_(std::move(asynclogName)) {
  }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    t(*rh_, req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    typename DeleteLike<Operation>::Type = 0) const {

    return fiber_local::runWithLocals([this, &req]() {
      fiber_local::setAsynclogName(asynclogName_);
      return rh_->route(req, Operation());
    });
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    OtherThanT(Operation, DeleteLike<>) = 0) const {

    return rh_->route(req, Operation());
  }
 private:
  const McrouterRouteHandlePtr rh_;
  const std::string asynclogName_;
};

}}}  // facebook::memcache::mcrouter
