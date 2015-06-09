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

#include <folly/experimental/fibers/AddTasks.h>

#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache {

/**
 * Sends the same request to all child route handles.
 * Returns the fastest non-error reply, or, if there are no non-error replies,
 * the last error reply.  All other requests complete asynchronously.
 */
template <class RouteHandleIf>
class AllFastestRoute {
 public:
  static std::string routeName() { return "all-fastest"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    return children_;
  }

  explicit AllFastestRoute(std::vector<std::shared_ptr<RouteHandleIf>> rh)
      : children_(std::move(rh)) {
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    typedef typename ReplyType<Operation, Request>::type Reply;
    if (children_.empty()) {
      return NullRoute<RouteHandleIf>::route(req, Operation());
    }

    /* Short circuit if one destination */
    if (children_.size() == 1) {
      return children_.back()->route(req, Operation());
    }

    std::vector<std::function<Reply()>> funcs;
    funcs.reserve(children_.size());
    auto reqCopy = std::make_shared<Request>(req.clone());
    for (auto& rh : children_) {
      funcs.push_back(
        [reqCopy, rh]() {
          return rh->route(*reqCopy, Operation());
        }
      );
    }

    auto taskIt = folly::fibers::addTasks(funcs.begin(), funcs.end());
    while (true) {
      auto reply = taskIt.awaitNext();
      if (!reply.isFailoverError() || !taskIt.hasNext()) {
        return reply;
      }
    }
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> children_;
};

}}
