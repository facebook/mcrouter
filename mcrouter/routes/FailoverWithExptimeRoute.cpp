/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "FailoverWithExptimeRoute.h"

#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  McrouterRouteHandlePtr normalTarget,
  std::vector<McrouterRouteHandlePtr> failoverTargets,
  uint32_t failoverExptime,
  FailoverWithExptimeSettings settings) {

  return makeMcrouterRouteHandle<FailoverWithExptimeRoute>(
    std::move(normalTarget),
    std::move(failoverTargets),
    failoverExptime,
    std::move(settings));
}

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  return makeMcrouterRouteHandle<FailoverWithExptimeRoute>(factory, json);
}

}}}
