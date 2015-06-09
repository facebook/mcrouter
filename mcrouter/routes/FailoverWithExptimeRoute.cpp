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

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/FailoverWithExptimeRouteIf.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeNullRoute();

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  McrouterRouteHandlePtr normalTarget,
  std::vector<McrouterRouteHandlePtr> failoverTargets,
  int32_t failoverExptime,
  FailoverErrorsSettings failoverErrors,
  bool failoverTagging) {

  if (!normalTarget) {
    return makeNullRoute();
  }

  if (failoverTargets.empty()) {
    return std::move(normalTarget);
  }

  return std::make_shared<McrouterRouteHandle<FailoverWithExptimeRoute>>(
    std::move(normalTarget),
    std::move(failoverTargets),
    failoverExptime,
    std::move(failoverErrors),
    failoverTagging);
}

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "FailoverWithExptimeRoute is not an object");

  McrouterRouteHandlePtr normal;
  if (auto jnormal = json.get_ptr("normal")) {
    normal = factory.create(*jnormal);
  }

  std::vector<McrouterRouteHandlePtr> failoverTargets;
  if (auto jfailover = json.get_ptr("failover")) {
    failoverTargets = factory.createList(*jfailover);
  }

  int32_t failoverExptime = 60;
  if (auto jexptime = json.get_ptr("failover_exptime")) {
    checkLogic(jexptime->isInt(), "FailoverWithExptimeRoute: "
                                  "failover_exptime is not an integer");
    failoverExptime = jexptime->getInt();
  }

  // Check if only one format is being used
  checkLogic(!(json.count("settings") && // old
        (json.count("failover_errors") || json.count("failover_tag"))), // new
      "Use either 'settings' (old format) or 'failover_errors' / 'failover_tag'"
    );

  // new format
  FailoverErrorsSettings failoverErrors;
  bool failoverTagging = false;
  if (auto jfailoverTag = json.get_ptr("failover_tag")) {
    checkLogic(jfailoverTag->isBool(),
               "FailoverWithExptime: failover_tag is not bool");
    failoverTagging = jfailoverTag->getBool();
  }
  if (auto jfailoverErrors = json.get_ptr("failover_errors")) {
    failoverErrors = FailoverErrorsSettings(*jfailoverErrors);
  }

  // old format
  if (auto jsettings = json.get_ptr("settings")) {
    VLOG(1) << "FailoverWithExptime: This config format is deprecated. "
               "Use 'failover_errors' instead of 'settings'.";
    auto oldSettings = FailoverWithExptimeSettings(*jsettings);
    failoverTagging = oldSettings.failoverTagging;
    failoverErrors = oldSettings.getFailoverErrors();
  }

  return makeFailoverWithExptimeRoute(
    std::move(normal),
    std::move(failoverTargets),
    failoverExptime,
    std::move(failoverErrors),
    failoverTagging);
}

}}}  // facebook::memcache::mcrouter
