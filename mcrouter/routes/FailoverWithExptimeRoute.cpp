/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FailoverWithExptimeRoute.h"

namespace facebook { namespace memcache { namespace mcrouter {

FailoverWithExptimeRoute::FailoverWithExptimeRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json)
    : failoverExptime_(60) {

  checkLogic(json.isObject(), "FailoverWithExptimeRoute is not object");

  std::vector<McrouterRouteHandlePtr> failoverTargets;

  if (auto failover = json.get_ptr("failover")) {
    failoverTargets = factory.createList(*failover);
  }

  failover_ = FailoverRoute<McrouterRouteHandleIf>(std::move(failoverTargets));

  if (auto normal = json.get_ptr("normal")) {
    normal_ = factory.create(*normal);
  }

  if (auto failoverExptime = json.get_ptr("failover_exptime")) {
    checkLogic(failoverExptime->isInt(),
               "failover_exptime is not integer");
    failoverExptime_ = failoverExptime->getInt();
  }

  // Check if only one format is being used
  checkLogic(!(json.count("settings") && // old
        (json.count("failover_errors") || json.count("failover_tag"))), // new
      "Use either 'settings' (old format) or 'failover_errors' / 'failover_tag'"
    );

  // new format
  if (auto failoverTag = json.get_ptr("failover_tag")) {
    checkLogic(failoverTag->isBool(),
               "FailoverWithExptime: failover_tag is not bool");
    failoverTagging_ = failoverTag->getBool();
  }
  if (auto failoverErrors = json.get_ptr("failover_errors")) {
    failoverErrors_ = FailoverErrorsSettings(*failoverErrors);
  }

  // old format
  if (auto settings = json.get_ptr("settings")) {
    VLOG(1) << "FailoverWithExptime: This config format is deprecated. "
               "Use 'failover_errors' instead of 'settings'.";
    auto oldSettings = FailoverWithExptimeSettings(*settings);
    failoverTagging_ = oldSettings.failoverTagging;
    failoverErrors_ = oldSettings.getFailoverErrors();
  }
}

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  McrouterRouteHandlePtr normalTarget,
  std::vector<McrouterRouteHandlePtr> failoverTargets,
  uint32_t failoverExptime,
  FailoverWithExptimeSettings settings) {

  return std::make_shared<McrouterRouteHandle<FailoverWithExptimeRoute>>(
    std::move(normalTarget),
    std::move(failoverTargets),
    failoverExptime,
    std::move(settings));
}

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  return std::make_shared<McrouterRouteHandle<FailoverWithExptimeRoute>>(
    factory, json);
}

}}}  // facebook::memcache::mcrouter
