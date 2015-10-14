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

#include "mcrouter/config.h"
#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/FailoverRateLimiter.h"

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
    if (fiber_local::getFailoverDisabled()) {
      t(*targets_[0], req, Operation());
    } else {
      t(targets_, req, Operation());
    }
  }

  FailoverRoute(std::vector<std::shared_ptr<RouteHandleIf>> targets,
                FailoverErrorsSettings failoverErrors,
                std::unique_ptr<FailoverRateLimiter> rateLimiter,
                bool failoverTagging)
      : targets_(std::move(targets)),
        failoverErrors_(std::move(failoverErrors)),
        rateLimiter_(std::move(rateLimiter)),
        failoverTagging_(failoverTagging) {
    assert(targets_.size() > 1);
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) {

    auto normalReply = targets_[0]->route(req, Operation());
    if (rateLimiter_) {
      rateLimiter_->bumpTotalReqs();
    }
    if (fiber_local::getSharedCtx()->failoverDisabled() ||
        !failoverErrors_.shouldFailover(normalReply, Operation())) {
      return normalReply;
    }

    if (rateLimiter_ && !rateLimiter_->failoverAllowed()) {
      return normalReply;
    }

    // Failover
    return fiber_local::runWithLocals([this, &req, &normalReply]() {
      fiber_local::setFailoverTag(failoverTagging_ && req.hasHashStop());
      fiber_local::addRequestClass(RequestClass::kFailover);
      auto doFailover = [this, &req, &normalReply](size_t i) {
        auto failoverReply = targets_[i]->route(req, Operation());
        logFailover(fiber_local::getSharedCtx()->proxy(),
                    Operation::name,
                    i,
                    targets_.size() - 1,
                    req,
                    normalReply,
                    failoverReply);
        return failoverReply;
      };
      for (size_t i = 1; i + 1 < targets_.size(); ++i) {
        auto failoverReply = doFailover(i);
        if (!failoverErrors_.shouldFailover(failoverReply, Operation())) {
          return failoverReply;
        }
      }
      return doFailover(targets_.size() - 1);
    });
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> targets_;
  const FailoverErrorsSettings failoverErrors_;
  std::unique_ptr<FailoverRateLimiter> rateLimiter_;
  const bool failoverTagging_{false};
};

}}} // facebook::memcache::mcrouter
