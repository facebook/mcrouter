/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cassert>
#include <chrono>

#include <folly/Format.h>
#include <folly/fibers/Baton.h>

#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

/**
 * Injects latency before and/or after sending the request down to it's child.
 */
template <class RouterInfo>
class LatencyInjectionRoute {
 public:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;
  using RouteHandlePtr = typename RouterInfo::RouteHandlePtr;

  /**
   * Constructs the latency injection route.
   *
   * @param rh              The child route handle
   * @param beforeLatency   Latency to inject before sending the request to "rh"
   * @param afterLatency    Latency to inject after sending the request to "rh"
   */
  LatencyInjectionRoute(
      RouteHandlePtr rh,
      std::chrono::milliseconds beforeLatency,
      std::chrono::milliseconds afterLatency)
      : rh_(std::move(rh)),
        beforeLatency_(beforeLatency),
        afterLatency_(afterLatency) {
    assert(rh_);
    assert(beforeLatency_.count() > 0 || afterLatency_.count() > 0);
  }

  std::string routeName() const {
    return folly::sformat(
        "latency-injection|before:{}ms|after:{}ms",
        beforeLatency_.count(),
        afterLatency_.count());
  }

  template <class Request>
  bool traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    return t(*rh_, req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    if (beforeLatency_.count() > 0) {
      folly::fibers::Baton beforeBaton;
      beforeBaton.try_wait_for(beforeLatency_);
    }

    auto reply = rh_->route(req);

    if (afterLatency_.count() > 0) {
      folly::fibers::Baton afterBaton;
      afterBaton.try_wait_for(afterLatency_);
    }

    return reply;
  }

 private:
  const RouteHandlePtr rh_;
  const std::chrono::milliseconds beforeLatency_;
  const std::chrono::milliseconds afterLatency_;
};

/**
 * Creates a LatencyInjectRoute from a json config.
 *
 * Sample json:
 * {
 *   "type": "LatencyInjectionRoute",
 *   "child": "PoolRoute|pool_name",
 *   "before_latency_ms": 10,
 *   "after_latency_ms": 20
 * }
 */
template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeLatencyInjectionRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "LoadBalancerRoute: config is not an object.");

  auto jChild = json.get_ptr("child");
  checkLogic(
      jChild != nullptr, "LatencyInjectionRoute: 'child' property is missing.");
  auto child = factory.create(*jChild);

  auto jBeforeLatency = json.get_ptr("before_latency_ms");
  auto jAfterLatency = json.get_ptr("after_latency_ms");
  checkLogic(
      jBeforeLatency != nullptr || jAfterLatency != nullptr,
      "LatencyInjectionRoute must specify either "
      "'before_latency_ms' or 'after_latency_ms'");

  std::chrono::milliseconds beforeLatency{0};
  std::chrono::milliseconds afterLatency{0};
  if (jBeforeLatency) {
    checkLogic(
        jBeforeLatency->isInt(),
        "LatencyInjectionRoute: 'before_latency_ms' must be an interger.");
    beforeLatency = std::chrono::milliseconds(jBeforeLatency->asInt());
  }
  if (jAfterLatency) {
    checkLogic(
        jAfterLatency->isInt(),
        "LatencyInjectionRoute: 'after_latency_ms' must be an interger.");
    afterLatency = std::chrono::milliseconds(jAfterLatency->asInt());
  }

  if (beforeLatency.count() == 0 && afterLatency.count() == 0) {
    // if we are not injecting any latency, optimize this rh away.
    return std::move(child);
  }

  return makeRouteHandleWithInfo<RouterInfo, LatencyInjectionRoute>(
      std::move(child), beforeLatency, afterLatency);
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
