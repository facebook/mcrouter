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

#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/routes/FailoverRoute.h"
#include "mcrouter/routes/HashRouteFactory.h"
#include "mcrouter/routes/LatestRoute.h"
#include "mcrouter/routes/LoggingRoute.h"
#include "mcrouter/routes/OperationSelectorRoute.h"
#include "mcrouter/routes/OutstandingLimitRoute.h"
#include "mcrouter/routes/ShadowRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

using McRouteHandleFactory = RouteHandleFactory<McrouterRouteHandleIf>;

McrouterRouteHandlePtr makeAllAsyncRoute(McRouteHandleFactory& factory,
                                         const folly::dynamic& json);

McrouterRouteHandlePtr makeAllFastestRoute(McRouteHandleFactory& factory,
                                           const folly::dynamic& json);

McrouterRouteHandlePtr makeAllInitialRoute(McRouteHandleFactory& factory,
                                           const folly::dynamic& json);

McrouterRouteHandlePtr makeAllMajorityRoute(McRouteHandleFactory& factory,
                                            const folly::dynamic& json);

McrouterRouteHandlePtr makeAllSyncRoute(McRouteHandleFactory& factory,
                                        const folly::dynamic& json);

McrouterRouteHandlePtr makeDevNullRoute(McRouteHandleFactory& factory,
                                        const folly::dynamic& json);

McrouterRouteHandlePtr makeHostIdRoute(McRouteHandleFactory& factory,
                                       const folly::dynamic& json);

McrouterRouteHandlePtr makeErrorRoute(McRouteHandleFactory& factory,
                                      const folly::dynamic& json);

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  McRouteHandleFactory& factory,
  const folly::dynamic& json);

McrouterRouteHandlePtr makeL1L2CacheRoute(McRouteHandleFactory& factory,
                                          const folly::dynamic& json);

McrouterRouteHandlePtr makeMigrateRoute(McRouteHandleFactory& factory,
                                        const folly::dynamic& json);

McrouterRouteHandlePtr makeMissFailoverRoute(McRouteHandleFactory& factory,
                                             const folly::dynamic& json);

McrouterRouteHandlePtr makeModifyExptimeRoute(McRouteHandleFactory& factory,
                                              const folly::dynamic& json);

McrouterRouteHandlePtr makeModifyKeyRoute(McRouteHandleFactory& factory,
                                          const folly::dynamic& json);

McrouterRouteHandlePtr makeRandomRoute(McRouteHandleFactory& factory,
                                       const folly::dynamic& json);

McrouterRouteHandlePtr makeWarmUpRoute(McRouteHandleFactory& factory,
                                       const folly::dynamic& json);

McrouterRouteHandlePtr makeAsynclogRoute(McrouterRouteHandlePtr rh,
                                         std::string asynclogName);

std::pair<McrouterRouteHandlePtr, std::string> parseAsynclogRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json);


template <>
std::shared_ptr<MemcacheRouteHandleIf>
McRouteHandleProvider<MemcacheRouterInfo>::createAsynclogRoute(
    std::shared_ptr<MemcacheRouteHandleIf> target,
    std::string asynclogName) {
  if (!proxy_.router().opts().asynclog_disable) {
    target = makeAsynclogRoute(std::move(target), asynclogName);
  }
  asyncLogRoutes_.emplace(std::move(asynclogName), target);
  return target;
}

template <>
std::unique_ptr<ExtraRouteHandleProviderIf<MemcacheRouterInfo>>
McRouteHandleProvider<MemcacheRouterInfo>::buildExtraProvider() {
  return createExtraRouteHandleProvider();
}

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
       [this](
           RouteHandleFactory<MemcacheRouteHandleIf>& factory,
           const folly::dynamic& json) {
         auto p = parseAsynclogRoute(factory, json);
         return createAsynclogRoute(std::move(p.first), std::move(p.second));
       }},
      {"DevNullRoute", &makeDevNullRoute},
      {"ErrorRoute", &makeErrorRoute},
      {"FailoverWithExptimeRoute", &makeFailoverWithExptimeRoute},
      {"HashRoute",
       [](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return makeHashRoute<McrouterRouterInfo>(factory, json);
       }},
      {"HostIdRoute", &makeHostIdRoute},
      {"L1L2CacheRoute", &makeL1L2CacheRoute},
      {"LatestRoute", &makeLatestRoute<MemcacheRouterInfo>},
      {"LoggingRoute", &makeLoggingRoute<MemcacheRouterInfo>},
      {"MigrateRoute", &makeMigrateRoute},
      {"MissFailoverRoute", &makeMissFailoverRoute},
      {"ModifyExptimeRoute", &makeModifyExptimeRoute},
      {"ModifyKeyRoute", &makeModifyKeyRoute},
      {"NullRoute", &makeNullRoute<MemcacheRouteHandleIf>},
      {"OperationSelectorRoute",
       &makeOperationSelectorRoute<MemcacheRouterInfo>},
      {"PoolRoute",
       [this](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return makePoolRoute(factory, json);
       }},
      {"PrefixPolicyRoute", &makeOperationSelectorRoute<MemcacheRouterInfo>},
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
