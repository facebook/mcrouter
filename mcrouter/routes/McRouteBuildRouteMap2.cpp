/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/routes/McRouteHandleProvider.h"

#include "mcrouter/routes/KeySplitRoute.h"
#include "mcrouter/routes/L1L2SizeSplitRoute.h"
#include "mcrouter/routes/LatestRoute.h"
#include "mcrouter/routes/LoadBalancerRoute.h"
#include "mcrouter/routes/LoggingRoute.h"
#include "mcrouter/routes/MigrateRouteFactory.h"
#include "mcrouter/routes/MissFailoverRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

using McRouteHandleFactory = RouteHandleFactory<McrouterRouteHandleIf>;
using MemcacheRouterInfo = facebook::memcache::MemcacheRouterInfo;

template <>
void McRouteHandleProvider<MemcacheRouterInfo>::buildRouteMap2(
    RouteHandleFactoryMap& map) {
  map.insert(std::make_pair("L1L2SizeSplitRoute", &makeL1L2SizeSplitRoute));
  map.insert(std::make_pair("KeySplitRoute", &makeKeySplitRoute));
  map.insert(
      std::make_pair("LatestRoute", &makeLatestRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "LoadBalancerRoute", &makeLoadBalancerRoute<MemcacheRouterInfo>));
  map.insert(
      std::make_pair("LoggingRoute", &makeLoggingRoute<MemcacheRouterInfo>));
  map.insert(
      std::make_pair("MigrateRoute", &makeMigrateRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "MissFailoverRoute", &makeMissFailoverRoute<MemcacheRouterInfo>));
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
