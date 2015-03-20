/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/routes/L1L2CacheRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeL1L2CacheRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  return makeMcrouterRouteHandle<L1L2CacheRoute>(factory, json);
}

McrouterRouteHandlePtr makeL1L2CacheRoute(
  McrouterRouteHandlePtr l1,
  McrouterRouteHandlePtr l2,
  uint32_t upgradingL1Exptime,
  size_t ncacheExptime,
  size_t ncacheUpdatePeriod) {
  return makeMcrouterRouteHandle<L1L2CacheRoute>(
    std::move(l1),
    std::move(l2),
    upgradingL1Exptime,
    ncacheExptime,
    ncacheUpdatePeriod);
}

}}}
