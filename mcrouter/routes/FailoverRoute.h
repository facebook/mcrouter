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
#include "mcrouter/LeaseTokenMap.h"
#include "mcrouter/lib/FailoverContext.h"
#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/McrouterInstance.h"
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

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    if (fiber_local::getFailoverDisabled()) {
      t(*targets_[0], req);
    } else {
      t(targets_, req);
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

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    return doRoute(req);
  }

  McReply route(const McRequestWithMcOp<mc_op_lease_set>& req) {
    if (!enableLeasePairing_) {
      return doRoute(req);
    }

    // Look into LeaseTokenMap
    auto& proxy = fiber_local::getSharedCtx()->proxy();
    auto& map = proxy.router().leaseTokenMap();
    if (auto item = map.query(name_, req.leaseToken())) {
      auto mutReq = req.clone();
      mutReq.setLeaseToken(item->originalToken);
      stat_incr(proxy.stats, redirected_lease_set_count_stat, 1);
      assert(targets_.size() > item->routeHandleChildIndex);
      return targets_[item->routeHandleChildIndex]->route(mutReq);
    }

    // If not found in the map, send to normal destiantion (don't failover)
    return targets_[0]->route(req);
  }

  TypedThriftReply<cpp2::McLeaseSetReply> route(
      const TypedThriftRequest<cpp2::McLeaseSetRequest>& req) {
    if (!enableLeasePairing_) {
      return doRoute(req);
    }

    // Look into LeaseTokenMap
    auto& proxy = fiber_local::getSharedCtx()->proxy();
    auto& map = proxy.router().leaseTokenMap();
    if (auto item = map.query(name_, req->get_leaseToken())) {
      auto mutReq = req.clone();
      mutReq->set_leaseToken(item->originalToken);
      stat_incr(proxy.stats, redirected_lease_set_count_stat, 1);
      assert(targets_.size() > item->routeHandleChildIndex);
      return targets_[item->routeHandleChildIndex]->route(mutReq);
    }

    // If not found in the map, send to normal destiantion (don't failover)
    return targets_[0]->route(req);
  }

  McReply route(const McRequestWithMcOp<mc_op_lease_get>& req) {
    size_t childIndex = 0;
    auto reply = doRoute(req, childIndex);

    if (!enableLeasePairing_ || reply.leaseToken() <= 1) {
      return reply;
    }

    auto& map = fiber_local::getSharedCtx()->proxy().router().leaseTokenMap();

    // If the lease token returned by the underlying route handle conflicts
    // with special tokens space, we need to store it in the map even if we
    // didn't failover.
    if (childIndex > 0 || map.conflicts(reply.leaseToken())) {
      auto specialToken = map.insert(name_,
                                     { reply.leaseToken(), childIndex });
      reply.setLeaseToken(specialToken);
    }

    return reply;
  }

  TypedThriftReply<cpp2::McLeaseGetReply> route(
      const TypedThriftRequest<cpp2::McLeaseGetRequest>& req) {

    size_t childIndex = 0;
    auto reply = doRoute(req, childIndex);

    const uint64_t leaseToken =
      reply->get_leaseToken() ? *reply->get_leaseToken() : 0;

    if (!enableLeasePairing_ || leaseToken <= 1) {
      return reply;
    }

    auto& map = fiber_local::getSharedCtx()->proxy().router().leaseTokenMap();

    // If the lease token returned by the underlying route handle conflicts
    // with special tokens space, we need to store it in the map even if we
    // didn't failover.
    if (childIndex > 0 || map.conflicts(leaseToken)) {
      auto specialToken = map.insert(name_,
                                     { leaseToken, childIndex });
      reply->set_leaseToken(specialToken);
    }

    return reply;
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> targets_;
  const FailoverErrorsSettings failoverErrors_;
  std::unique_ptr<FailoverRateLimiter> rateLimiter_;
  const bool failoverTagging_{false};
  FailoverPolicyT failoverPolicy_;
  const bool enableLeasePairing_{false};
  const std::string name_;

  template <class Request>
  inline ReplyT<Request> doRoute(const Request& req) {
    size_t tmp;
    return doRoute(req, tmp);
  }

  template <class Request>
  ReplyT<Request> doRoute(const Request& req, size_t& childIndex) {
    auto normalReply = targets_[0]->route(req);
    childIndex = 0;
    if (rateLimiter_) {
      rateLimiter_->bumpTotalReqs();
    }
    if (fiber_local::getSharedCtx()->failoverDisabled() ||
        !failoverErrors_.shouldFailover(normalReply, req)) {
      return normalReply;
    }

    auto& proxy = fiber_local::getSharedCtx()->proxy();
    stat_incr(proxy.stats, failover_all_stat, 1);

    if (rateLimiter_ && !rateLimiter_->failoverAllowed()) {
      stat_incr(proxy.stats, failover_rate_limited_stat, 1);
      return normalReply;
    }

    // Failover
    return fiber_local::runWithLocals([this, &req, &proxy,
                                       &normalReply, &childIndex]() {
      fiber_local::setFailoverTag(failoverTagging_ && req.hasHashStop());
      fiber_local::addRequestClass(RequestClass::kFailover);
      auto doFailover = [this, &req, &proxy, &normalReply](
          typename FailoverPolicyT::Iterator& child) {
        auto failoverReply = child->route(req);
        FailoverContext failoverContext(child.getTrueIndex(),
                                        targets_.size() - 1,
                                        req,
                                        normalReply,
                                        failoverReply);
        logFailover(proxy, failoverContext);
        return failoverReply;
      };


      auto cur = failoverPolicy_.begin();
      // set the index of the child that generated the reply.
      SCOPE_EXIT {
        childIndex = cur.getTrueIndex();
      };
      auto nx = cur;
      for (++nx; nx != failoverPolicy_.end(); ++cur, ++nx) {
        auto failoverReply = doFailover(cur);
        if (!failoverErrors_.shouldFailover(failoverReply, req)) {
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
};

}}} // facebook::memcache::mcrouter
