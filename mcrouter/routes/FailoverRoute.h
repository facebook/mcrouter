/*
 *  Copyright (c) 2016, Facebook, Inc.
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
template <class RouteHandleIf, typename FailoverPolicyT>
class FailoverRoute {
 public:
  std::string routeName() const {
    if (name_.empty()) {
      return "failover";
    }
    return "failover:" + name_;
  }

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
                bool failoverTagging,
                bool enableLeasePairing,
                std::string name,
                const folly::dynamic& policyConfig = nullptr)
      : targets_(std::move(targets)),
        failoverErrors_(std::move(failoverErrors)),
        rateLimiter_(std::move(rateLimiter)),
        failoverTagging_(failoverTagging),
        failoverPolicy_(targets_, policyConfig),
        enableLeasePairing_(enableLeasePairing),
        name_(std::move(name)) {
    assert(targets_.size() > 1);
    assert(!enableLeasePairing_ || !name_.empty());
  }

  template <class Operation, class Request>
  ReplyT<Operation, Request> route(const Request& req, Operation) {
    auto normalReply = targets_[0]->route(req, Operation());
    if (rateLimiter_) {
      rateLimiter_->bumpTotalReqs();
    }
    if (fiber_local::getSharedCtx()->failoverDisabled() ||
        !failoverErrors_.shouldFailover(normalReply, Operation())) {
      return normalReply;
    }

    auto& proxy = fiber_local::getSharedCtx()->proxy();
    stat_incr(proxy.stats, failover_all_stat, 1);

    if (rateLimiter_ && !rateLimiter_->failoverAllowed()) {
      stat_incr(proxy.stats, failover_rate_limited_stat, 1);
      return normalReply;
    }

    // Failover
    return fiber_local::runWithLocals([this, &req, &proxy, &normalReply]() {
      fiber_local::setFailoverTag(failoverTagging_ && req.hasHashStop());
      fiber_local::addRequestClass(RequestClass::kFailover);
      auto doFailover = [this, &req, &proxy, &normalReply](
          typename FailoverPolicyT::Iterator& child) {
        auto failoverReply = child->route(req, Operation());
        logFailover(proxy,
                    Operation::name,
                    child.getTrueIndex(),
                    targets_.size() - 1,
                    req,
                    normalReply,
                    failoverReply);
        return failoverReply;
      };

      auto cur = failoverPolicy_.begin();
      auto nx = cur;
      for (++nx; nx != failoverPolicy_.end(); ++cur, ++nx) {
        auto failoverReply = doFailover(cur);
        if (!failoverErrors_.shouldFailover(failoverReply, Operation())) {
          return failoverReply;
        }
      }

      auto failoverReply = doFailover(cur);
      if (failoverReply.isError()) {
        stat_incr(proxy.stats, failover_all_failed_stat, 1);
      }

      return failoverReply;
    });
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> targets_;
  const FailoverErrorsSettings failoverErrors_;
  std::unique_ptr<FailoverRateLimiter> rateLimiter_;
  const bool failoverTagging_{false};
  FailoverPolicyT failoverPolicy_;
  const bool enableLeasePairing_{false};
  const std::string name_;
};

}}} // facebook::memcache::mcrouter
