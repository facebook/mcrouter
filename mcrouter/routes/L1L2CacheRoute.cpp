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
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/routes/L1L2CacheRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

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

McrouterRouteHandlePtr makeL1L2CacheRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "L1L2CacheRoute should be an object");
  checkLogic(json.count("l1"), "L1L2CacheRoute: no l1 route");
  checkLogic(json.count("l2"), "L1L2CacheRoute: no l2 route");
  checkLogic(json.count("upgradingL1Exptime"),
             "L1L2CacheRoute: no upgradingL1Exptime");
  checkLogic(json["upgradingL1Exptime"].isInt(),
             "L1L2CacheRoute: upgradingL1Exptime is not an integer");
  uint32_t upgradingL1Exptime = json["upgradingL1Exptime"].getInt();

  size_t ncacheExptime = 0;
  if (json.count("ncacheExptime")) {
    checkLogic(json["ncacheExptime"].isInt(),
               "L1L2CacheRoute: ncacheExptime is not an integer");
    ncacheExptime = json["ncacheExptime"].getInt();
  }

  size_t ncacheUpdatePeriod = 0;
  if (json.count("ncacheUpdatePeriod")) {
    checkLogic(json["ncacheUpdatePeriod"].isInt(),
               "L1L2CacheRoute: ncacheUpdatePeriod is not an integer");
    ncacheUpdatePeriod = json["ncacheUpdatePeriod"].getInt();
  }

  return makeL1L2CacheRoute(
    factory.create(json["l1"]),
    factory.create(json["l2"]),
    upgradingL1Exptime,
    ncacheExptime,
    ncacheUpdatePeriod);
}

}}}  // facebook::memcache::mcrouter
