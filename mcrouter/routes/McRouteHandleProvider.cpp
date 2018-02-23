/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McRouteHandleProvider.h"

#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/routes/AllAsyncRouteFactory.h"
#include "mcrouter/routes/AllFastestRouteFactory.h"
#include "mcrouter/routes/AllInitialRouteFactory.h"
#include "mcrouter/routes/AllMajorityRouteFactory.h"
#include "mcrouter/routes/AllSyncRouteFactory.h"
#include "mcrouter/routes/DevNullRoute.h"
#include "mcrouter/routes/ErrorRoute.h"
#include "mcrouter/routes/FailoverRoute.h"
#include "mcrouter/routes/FailoverWithExptimeRouteFactory.h"
#include "mcrouter/routes/HashRouteFactory.h"
#include "mcrouter/routes/HostIdRouteFactory.h"
#include "mcrouter/routes/L1L2CacheRouteFactory.h"
#include "mcrouter/routes/L1L2SizeSplitRoute.h"
#include "mcrouter/routes/LatestRoute.h"
#include "mcrouter/routes/LoadBalancerRoute.h"
#include "mcrouter/routes/LoggingRoute.h"
#include "mcrouter/routes/McExtraRouteHandleProvider.h"
#include "mcrouter/routes/MigrateRouteFactory.h"
#include "mcrouter/routes/MissFailoverRoute.h"
#include "mcrouter/routes/ModifyExptimeRoute.h"
#include "mcrouter/routes/ModifyKeyRoute.h"
#include "mcrouter/routes/OperationSelectorRoute.h"
#include "mcrouter/routes/OutstandingLimitRoute.h"
#include "mcrouter/routes/RandomRouteFactory.h"
#include "mcrouter/routes/ShadowRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

using McRouteHandleFactory = RouteHandleFactory<McrouterRouteHandleIf>;

McrouterRouteHandlePtr makeWarmUpRoute(
    McRouteHandleFactory& factory,
    const folly::dynamic& json);

template <>
std::unique_ptr<ExtraRouteHandleProviderIf<MemcacheRouterInfo>>
McRouteHandleProvider<MemcacheRouterInfo>::buildExtraProvider() {
  return std::make_unique<McExtraRouteHandleProvider<MemcacheRouterInfo>>();
}

template <>
typename McRouteHandleProvider<MemcacheRouterInfo>::RouteHandleFactoryMap
McRouteHandleProvider<MemcacheRouterInfo>::buildRouteMap() {
  RouteHandleFactoryMap map{
      {"AllAsyncRoute", &makeAllAsyncRoute<MemcacheRouterInfo>},
      {"AllFastestRoute", &makeAllFastestRoute<MemcacheRouterInfo>},
      {"AllInitialRoute", &makeAllInitialRoute<MemcacheRouterInfo>},
      {"AllMajorityRoute", &makeAllMajorityRoute<MemcacheRouterInfo>},
      {"AllSyncRoute", &makeAllSyncRoute<MemcacheRouterInfo>},
      {"DevNullRoute", &makeDevNullRoute<MemcacheRouterInfo>},
      {"ErrorRoute", &makeErrorRoute<MemcacheRouterInfo>},
      {"FailoverWithExptimeRoute",
       &makeFailoverWithExptimeRoute<MemcacheRouterInfo>},
      {"HashRoute",
       [](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return makeHashRoute<McrouterRouterInfo>(factory, json);
       }},
      {"HostIdRoute", &makeHostIdRoute<MemcacheRouterInfo>},
      {"L1L2CacheRoute", &makeL1L2CacheRoute<MemcacheRouterInfo>},
      {"L1L2SizeSplitRoute", &makeL1L2SizeSplitRoute},
      {"LatestRoute", &makeLatestRoute<MemcacheRouterInfo>},
      {"LoadBalancerRoute", &makeLoadBalancerRoute<MemcacheRouterInfo>},
      {"LoggingRoute", &makeLoggingRoute<MemcacheRouterInfo>},
      {"MigrateRoute", &makeMigrateRoute<MemcacheRouterInfo>},
      {"MissFailoverRoute", &makeMissFailoverRoute<MemcacheRouterInfo>},
      {"ModifyKeyRoute", &makeModifyKeyRoute<MemcacheRouterInfo>},
      {"ModifyExptimeRoute", &makeModifyExptimeRoute<MemcacheRouterInfo>},
      {"NullRoute", &makeNullRoute<MemcacheRouteHandleIf>},
      {"OperationSelectorRoute",
       &makeOperationSelectorRoute<MemcacheRouterInfo>},
      {"PoolRoute",
       [this](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return makePoolRoute(factory, json);
       }},
      {"PrefixPolicyRoute", &makeOperationSelectorRoute<MemcacheRouterInfo>},
      {"RandomRoute", &makeRandomRoute<MemcacheRouterInfo>},
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
