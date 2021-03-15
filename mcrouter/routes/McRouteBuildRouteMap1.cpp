/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/routes/McRouteHandleProvider.h"

#include "mcrouter/routes/DevNullRoute.h"
#include "mcrouter/routes/ErrorRoute.h"
#include "mcrouter/routes/FailoverRoute.h"
#include "mcrouter/routes/FailoverWithExptimeRouteFactory.h"
#include "mcrouter/routes/HashRouteFactory.h"
#include "mcrouter/routes/HostIdRouteFactory.h"
#include "mcrouter/routes/L1L2CacheRouteFactory.h"
#include "mcrouter/routes/LatencyInjectionRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

using McRouteHandleFactory = RouteHandleFactory<McrouterRouteHandleIf>;
using MemcacheRouterInfo = facebook::memcache::MemcacheRouterInfo;

template <>
void McRouteHandleProvider<MemcacheRouterInfo>::buildRouteMap1(
    RouteHandleFactoryMap& map) {
  map.insert(
      std::make_pair("DevNullRoute", &makeDevNullRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair("ErrorRoute", &makeErrorRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "FailoverWithExptimeRoute",
      &makeFailoverWithExptimeRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "HashRoute",
      [](McRouteHandleFactory& factory, const folly::dynamic& json) {
        return makeHashRoute<McrouterRouterInfo>(factory, json);
      }));
  map.insert(
      std::make_pair("HostIdRoute", &makeHostIdRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "LatencyInjectionRoute", &makeLatencyInjectionRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "L1L2CacheRoute", &makeL1L2CacheRoute<MemcacheRouterInfo>));
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
