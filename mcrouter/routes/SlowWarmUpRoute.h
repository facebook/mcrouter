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

#include <string>

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/SlowWarmUpRouteSettings.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * This route handle allows slow warm up of cold memcached boxes. All it does is
 * route to "failoverRoute" straight away if the box is cold (instead of wasting
 * a network roundtrip), so it's purpose is to be used together with a failover
 * route handle.
 *
 * One SlowWarmUpRoute is created for each ProxyDestination, which allows us to
 * keep state related to the destination.
 *
 * This route handle is flexible and can be configured via the "settings"
 * property. Bellow is a list of parameters that can be tweaked to adjust
 * the behavior of this route handle:
 *
 * "enable_threshold": Threshold (double between 0 and 1) that will be used to
 *                     put the server in the warmup state. Whenever the hit rate
 *                     of the server goes bellow that threshold, we enter warmup
 *                     state.
 * "disable_threshold": Threshold (double between 0 and 1) that will be used to
 *                      remove the server from the warmup state. Whenver the hit
 *                      rate goes up that threshold, we exit warmup state (if
 *                      the box is being warmed up).
 * "start": Fraction (double between 0 and 1) of requests that we should send
 *          to the server being warmed up when its hit rate is 0.
 * "step": Step by which we increment the percentage of requests sent to the
 *         server.
 * "min_requests": Minimum number of requests necessary to start calculating
 *                 the hit rate. Before that number is reached, the destination
 *                 is considered "warm".
 *
 * To summarize, if a server is being warmed up, the percentage of requests to
 * send to server is calculated by the formula:
 *    start + (step * hitRate)
 */
template <class RouteHandleIf>
class SlowWarmUpRoute {
 public:
  static std::string routeName() { return "slow-warmup"; }

  SlowWarmUpRoute(std::shared_ptr<RouteHandleIf> target,
                  std::shared_ptr<RouteHandleIf> failoverTarget,
                  std::shared_ptr<SlowWarmUpRouteSettings> settings)
      : target_(std::move(target)),
        failoverTarget_(std::move(failoverTarget)),
        settings_(std::move(settings)) {
  }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*target_, req, Operation());
    t(*failoverTarget_, req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
      const Request& req, Operation,
      typename GetLike<Operation>::Type = 0) const {

    auto& proxy = fiber_local::getSharedCtx()->proxy();
    if (warmingUp() && !shouldSendRequest(proxy.randomGenerator)) {
      return fiber_local::runWithLocals([this, &req]() {
        fiber_local::addRequestClass(RequestClass::kFailover);
        return failoverTarget_->route(req, Operation());
      });
    }

    return routeImpl(req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
      const Request& req, Operation,
      OtherThanT(Operation, GetLike<>) = 0) const {
    return routeImpl(req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
      const Request& req, Operation) const {

    auto reply = target_->route(req, Operation());
    if (reply.isHit()) {
      ++stats_.hits;
    } else if (reply.isMiss()) {
      ++stats_.misses;
    }
    return std::move(reply);
  }

 private:
  struct WarmUpStats {
    uint64_t hits{0};
    uint64_t misses{0};
    bool enabled{false};
  };

  const std::shared_ptr<RouteHandleIf> target_;
  const std::shared_ptr<RouteHandleIf> failoverTarget_;
  const std::shared_ptr<SlowWarmUpRouteSettings> settings_;
  mutable WarmUpStats stats_;

  bool warmingUp() const {
    if (stats_.enabled) {
      stats_.enabled = hitRate() < settings_->disableThreshold();
    } else {
      stats_.enabled = hitRate() < settings_->enableThreshold();
    }
    return stats_.enabled;
  }

  double hitRate() const {
    uint64_t total = stats_.hits + stats_.misses;
    if (total < settings_->minRequests()) {
      return 1.0;
    }
    return stats_.hits / static_cast<double>(total);
  }

  template <class RNG>
  bool shouldSendRequest(RNG& rng) const {
    double target = settings_->start() + (hitRate() * settings_->step());
    return std::generate_canonical<double,
           std::numeric_limits<double>::digits>(rng) <= target;
  }
};

}}} // facebook::memcache::mcrouter
