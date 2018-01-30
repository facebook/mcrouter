/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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

#include <folly/fibers/AddTasks.h>

#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"

namespace facebook {
namespace memcache {

/**
 * Sends the same request to all child route handles.
 * Returns the fastest non-error reply, or, if there are no non-error replies,
 * the last error reply.  All other requests complete asynchronously.
 */
template <class RouteHandleIf>
class AllFastestRoute {
 public:
  static std::string routeName() {
    return "all-fastest";
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(children_, req);
  }

  explicit AllFastestRoute(std::vector<std::shared_ptr<RouteHandleIf>> rh)
      : children_(std::move(rh)) {
    assert(!children_.empty());
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    using Reply = ReplyT<Request>;

    std::vector<std::function<Reply()>> funcs;
    funcs.reserve(children_.size());
    auto reqCopy = std::make_shared<Request>(req);
    for (auto& rh : children_) {
      funcs.push_back([reqCopy, rh]() { return rh->route(*reqCopy); });
    }

    auto taskIt = folly::fibers::addTasks(funcs.begin(), funcs.end());
    while (true) {
      auto reply = taskIt.awaitNext();
      if (!isFailoverErrorResult(reply.result()) || !taskIt.hasNext()) {
        return reply;
      }
    }
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> children_;
};
}
} // facebook::memcache
