/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/routes/FailoverRateLimiter.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/ModifyExptimeRoute.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

std::vector<McrouterRouteHandlePtr> getFailoverChildren(
    McrouterRouteHandlePtr normal,
    std::vector<McrouterRouteHandlePtr> failover,
    int32_t failoverExptime) {

  std::vector<McrouterRouteHandlePtr> children;
  children.push_back(std::move(normal));
  for (auto& frh : failover) {
    auto rh = std::make_shared<McrouterRouteHandle<ModifyExptimeRoute>>(
        std::move(frh), failoverExptime, ModifyExptimeRoute::Action::Min);
    children.push_back(std::move(rh));
  }
  return children;
}

}  // anonymous

McrouterRouteHandlePtr makeFailoverRoute(
    const folly::dynamic& json,
    std::vector<McrouterRouteHandlePtr> children);

McrouterRouteHandlePtr makeFailoverRoute(
    std::vector<McrouterRouteHandlePtr> rh,
    FailoverErrorsSettings failoverErrors,
    std::unique_ptr<FailoverRateLimiter> rateLimiter,
    bool failoverTagging);

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
    McrouterRouteHandlePtr normal,
    std::vector<McrouterRouteHandlePtr> failover,
    int32_t failoverExptime,
    FailoverErrorsSettings failoverErrors,
    std::unique_ptr<FailoverRateLimiter> rateLimiter) {
  auto children = getFailoverChildren(std::move(normal),
                                      std::move(failover),
                                      failoverExptime);
  return makeFailoverRoute(std::move(children),
                           std::move(failoverErrors),
                           std::move(rateLimiter),
                           /* failoverTagging */ false);
}

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "FailoverWithExptimeRoute is not an object");
  auto jnormal = json.get_ptr("normal");
  checkLogic(jnormal, "FailoverWithExptimeRoute: normal not found");
  auto normal = factory.create(*jnormal);

  int32_t failoverExptime = 60;
  if (auto jexptime = json.get_ptr("failover_exptime")) {
    checkLogic(jexptime->isInt(), "FailoverWithExptimeRoute: "
                                  "failover_exptime is not an integer");
    failoverExptime = jexptime->getInt();
  }

  std::vector<McrouterRouteHandlePtr> failover;
  if (auto jfailover = json.get_ptr("failover")) {
    failover = factory.createList(*jfailover);
  }

  auto children = getFailoverChildren(std::move(normal),
                                      std::move(failover),
                                      failoverExptime);
  return makeFailoverRoute(json, std::move(children));
}

}}}  // facebook::memcache::mcrouter
