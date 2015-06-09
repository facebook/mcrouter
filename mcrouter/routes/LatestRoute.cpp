/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/routes/LatestRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeNullRoute();

McrouterRouteHandlePtr makeLatestRoute(
  std::vector<McrouterRouteHandlePtr> targets,
  size_t failoverCount,
  FailoverErrorsSettings failoverErrors) {

  if (targets.empty() || failoverCount == 0) {
    return makeNullRoute();
  }

  if (targets.size() == 1) {
    return std::move(targets[0]);
  }

  return makeMcrouterRouteHandle<LatestRoute>(
    std::move(targets),
    failoverCount,
    std::move(failoverErrors));
}

McrouterRouteHandlePtr makeLatestRoute(
  const folly::dynamic& json,
  std::vector<McrouterRouteHandlePtr> targets) {

  size_t failoverCount = 5;
  FailoverErrorsSettings failoverErrors;
  if (json.isObject()) {
    if (auto jfailoverCount = json.get_ptr("failover_count")) {
      checkLogic(jfailoverCount->isInt(),
                 "LatestRoute: failover_count is not an integer");
      failoverCount = jfailoverCount->getInt();
    }
    if (auto jFailoverErrors = json.get_ptr("failover_errors")) {
      failoverErrors = FailoverErrorsSettings(*jFailoverErrors);
    }
  }
  return makeLatestRoute(std::move(targets), failoverCount,
                         std::move(failoverErrors));
}

McrouterRouteHandlePtr makeLatestRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  std::vector<McrouterRouteHandlePtr> children;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
  } else {
    children = factory.createList(json);
  }
  return makeLatestRoute(json, std::move(children));
}

}}}  // facebook::memcache::mcrouter
