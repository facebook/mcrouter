/*
 *  Copyright (c) 2017, Facebook, Inc.
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

#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/LeaseTokenMap.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyRequestContextTyped.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/FailoverContext.h"
#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/routes/FailoverPolicy.h"
#include "mcrouter/routes/FailoverRateLimiter.h"

namespace folly {
struct dynamic;
}

namespace facebook {
namespace memcache {

template <class RouteHandleIf>
class RouteHandleFactory;

namespace mcrouter {

/**
 * Sends the same request sequentially to each destination in the list in order,
 * until the first non-error reply.  If all replies result in errors, returns
 * the last destination's reply.
 */
template <class RouterInfo, typename FailoverPolicyT>
class FailoverRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;

 public:
  std::string routeName() const {
    if (name_.empty()) {
      return "failover";
    }
    return "failover:" + name_;
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    if (fiber_local<RouterInfo>::getFailoverDisabled()) {
      t(*targets_[0], req);
    } else {
      t(targets_, req);
    }
  }

  FailoverRoute(
      std::vector<std::shared_ptr<RouteHandleIf>> targets,
      FailoverErrorsSettings failoverErrors,
      std::unique_ptr<FailoverRateLimiter> rateLimiter,
      bool failoverTagging,
      bool enableLeasePairing,
      std::string name,
      const folly::dynamic& policyConfig)
      : name_(std::move(name)),
        targets_(std::move(targets)),
        failoverErrors_(std::move(failoverErrors)),
        rateLimiter_(std::move(rateLimiter)),
        failoverTagging_(failoverTagging),
        failoverPolicy_(targets_, policyConfig),
        enableLeasePairing_(enableLeasePairing) {
    assert(targets_.size() > 1);
    assert(!enableLeasePairing_ || !name_.empty());
  }

  virtual ~FailoverRoute() {}

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    return doRoute(req);
  }

  McLeaseSetReply route(const McLeaseSetRequest& req) {
    if (!enableLeasePairing_) {
      return doRoute(req);
    }

    // Look into LeaseTokenMap
    auto& proxy = fiber_local<RouterInfo>::getSharedCtx()->proxy();
    auto& map = proxy.router().leaseTokenMap();
    if (auto item = map.query(name_, req.leaseToken())) {
      auto mutReq = req;
      mutReq.leaseToken() = item->originalToken;
      proxy.stats().increment(redirected_lease_set_count_stat);
      assert(targets_.size() > item->routeHandleChildIndex);
      return targets_[item->routeHandleChildIndex]->route(mutReq);
    }

    // If not found in the map, send to normal destiantion (don't failover)
    return targets_[0]->route(req);
  }

  McLeaseGetReply route(const McLeaseGetRequest& req) {
    size_t childIndex = 0;
    auto reply = doRoute(req, childIndex);
    const auto leaseToken = reply.leaseToken();

    if (!enableLeasePairing_ || leaseToken <= 1) {
      return reply;
    }

    auto& map = fiber_local<RouterInfo>::getSharedCtx()
                    ->proxy()
                    .router()
                    .leaseTokenMap();

    // If the lease token returned by the underlying route handle conflicts
    // with special tokens space, we need to store it in the map even if we
    // didn't failover.
    if (childIndex > 0 || map.conflicts(leaseToken)) {
      auto specialToken =
          map.insert(name_, {static_cast<uint64_t>(leaseToken), childIndex});
      reply.leaseToken() = specialToken;
    }

    return reply;
  }

 protected:
  const std::string name_;

  virtual bool additionalFailoverCheck(
      mc_res_t /* unused */,
      folly::StringPiece /* unused */,
      const folly::IOBuf& /* unused */) const {
    return false;
  }

 private:
  enum class FailoverType {
    NONE,
    NORMAL,
    CONDITIONAL,
  };

  const std::vector<std::shared_ptr<RouteHandleIf>> targets_;
  const FailoverErrorsSettings failoverErrors_;
  std::unique_ptr<FailoverRateLimiter> rateLimiter_;
  const bool failoverTagging_{false};
  FailoverPolicyT failoverPolicy_;
  const bool enableLeasePairing_{false};

  template <class Request>
  inline ReplyT<Request> doRoute(const Request& req) {
    size_t tmp;
    return doRoute(req, tmp);
  }

  template <class Request>
  ReplyT<Request> doRoute(const Request& req, size_t& childIndex) {
    auto& proxy = fiber_local<RouterInfo>::getSharedCtx()->proxy();

    bool conditionalFailover = false;
    SCOPE_EXIT {
      if (conditionalFailover) {
        proxy.stats().increment(failover_conditional_stat);
      }
    };

    auto normalReply = targets_[0]->route(req);
    childIndex = 0;
    if (rateLimiter_) {
      rateLimiter_->bumpTotalReqs();
    }
    if (fiber_local<RouterInfo>::getSharedCtx()->failoverDisabled()) {
      return normalReply;
    }
    switch (shouldFailover(normalReply, req)) {
      case FailoverType::NONE:
        return normalReply;
      case FailoverType::CONDITIONAL:
        conditionalFailover = true;
        break;
      default:
        break;
    }

    proxy.stats().increment(failover_all_stat);

    if (rateLimiter_ && !rateLimiter_->failoverAllowed()) {
      proxy.stats().increment(failover_rate_limited_stat);
      return normalReply;
    }

    // Failover
    return fiber_local<RouterInfo>::runWithLocals(
        [this,
         &req,
         &proxy,
         &normalReply,
         &childIndex,
         &conditionalFailover]() {
          fiber_local<RouterInfo>::setFailoverTag(failoverTagging_);
          fiber_local<RouterInfo>::addRequestClass(RequestClass::kFailover);
          auto doFailover = [this, &req, &proxy, &normalReply](
              typename FailoverPolicyT::Iterator& child) {
            auto failoverReply = child->route(req);
            FailoverContext failoverContext(
                child.getTrueIndex(),
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
            switch (shouldFailover(failoverReply, req)) {
              case FailoverType::NONE:
                return failoverReply;
              case FailoverType::CONDITIONAL:
                conditionalFailover = true;
                break;
              default:
                break;
            }
          }

          auto failoverReply = doFailover(cur);
          if (isErrorResult(failoverReply.result())) {
            proxy.stats().increment(failover_all_failed_stat);
          }

          return failoverReply;
        });
  }

  // TODO t15812771 update this to work with all UpdateLike requests
  FailoverType shouldFailover(const McSetReply& reply, const McSetRequest& req)
      const {
    if (failoverErrors_.shouldFailover(reply, req)) {
      return FailoverType::NORMAL;
    }
    if (additionalFailoverCheck(
            reply.result(), req.key().fullKey(), req.value())) {
      return FailoverType::CONDITIONAL;
    }
    return FailoverType::NONE;
  }

  template <class Request>
  FailoverType shouldFailover(const ReplyT<Request>& reply, const Request& req)
      const {
    return failoverErrors_.shouldFailover(reply, req) ? FailoverType::NORMAL
                                                      : FailoverType::NONE;
  }
};

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeFailoverRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json,
    ExtraRouteHandleProviderIf<RouterInfo>& extraProvider);

} // mcrouter
} // memcache
} // facebook

#include "mcrouter/routes/FailoverRoute-inl.h"
