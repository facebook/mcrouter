/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McRouteHandleProvider.h"

#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <>
typename McRouteHandleProvider<MemcacheRouterInfo>::RouteHandleFactoryMap
McRouteHandleProvider<MemcacheRouterInfo>::buildRouteMap() {
  RouteHandleFactoryMap map{
      {"AllAsyncRoute", &makeAllAsyncRoute},
      {"AllFastestRoute", &makeAllFastestRoute},
      {"AllInitialRoute", &makeAllInitialRoute},
      {"AllMajorityRoute", &makeAllMajorityRoute},
      {"AllSyncRoute", &makeAllSyncRoute},
      {"AsynclogRoute",
       [this](RouteHandleFactory<MemcacheRouteHandleIf>& factory,
              const folly::dynamic& json) {
         auto p = parseAsynclogRoute(factory, json);
         return createAsynclogRoute(std::move(p.first), std::move(p.second));
       }
      },
      {"DevNullRoute", &makeDevNullRoute},
      {"ErrorRoute", &makeErrorRoute},
      {"FailoverWithExptimeRoute", &makeFailoverWithExptimeRoute},
      {"HashRoute",
       [](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return makeHashRoute(factory, json);
       }},
      {"HostIdRoute", &makeHostIdRoute},
      {"L1L2CacheRoute", &makeL1L2CacheRoute},
      {"LatestRoute", &makeLatestRoute},
      {"LoggingRoute", &makeLoggingRoute},
      {"MigrateRoute", &makeMigrateRoute},
      {"MissFailoverRoute", &makeMissFailoverRoute},
      {"ModifyExptimeRoute", &makeModifyExptimeRoute},
      {"ModifyKeyRoute", &makeModifyKeyRoute},
      {"NullRoute", &makeNullRoute<MemcacheRouteHandleIf>},
      {"OperationSelectorRoute", &makeOperationSelectorRoute},
      {"PoolRoute",
       [this](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return makePoolRoute(factory, json);
       }},
      {"PrefixPolicyRoute", &makeOperationSelectorRoute},
      {"RandomRoute", &makeRandomRoute},
      {"RateLimitRoute",
       [](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return makeRateLimitRoute(factory, json);
       }},
      {"WarmUpRoute", &makeWarmUpRoute},
  };
  return map;
}

} // mcrouter
} // memcache
} // facebook
