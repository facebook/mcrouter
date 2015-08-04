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

#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyRequestContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Sends the same request sequentially to each destination in the list in order,
 * until the first non-error reply.  If all replies result in errors, returns
 * the last destination's reply.
 */
template <class RouteHandleIf>
class FailoverRoute {
 public:
  static std::string routeName() { return "failover"; }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(targets_, req, Operation());
  }

  FailoverRoute(std::vector<std::shared_ptr<RouteHandleIf>> targets,
                FailoverErrorsSettings failoverErrors,
                bool failoverTagging)
      : targets_(std::move(targets)),
        failoverErrors_(std::move(failoverErrors)),
        failoverTagging_(failoverTagging) {
    assert(targets_.size() > 1);
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    auto reply = targets_[0]->route(req, Operation());
    if (fiber_local::getSharedCtx()->failoverDisabled() ||
        !failoverErrors_.shouldFailover(reply, Operation())) {
      return reply;
    }

    // Failover
    bool needFailoverTag = failoverTagging_ && req.hasHashStop();
    return fiber_local::runWithLocals([this, &req, needFailoverTag]() {
      fiber_local::setFailoverTag(needFailoverTag);
      for (size_t i = 1; i + 1 < targets_.size(); ++i) {
        fiber_local::setRequestClass(RequestClass::FAILOVER);
        auto failoverReply = targets_[i]->route(req, Operation());
        if (!failoverErrors_.shouldFailover(failoverReply, Operation())) {
          return failoverReply;
        }
      }
      fiber_local::setRequestClass(RequestClass::FAILOVER);
      return targets_.back()->route(req, Operation());
    });
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> targets_;
  const FailoverErrorsSettings failoverErrors_;
  const bool failoverTagging_{false};
};

}}} // facebook::memcache::mcrouter
