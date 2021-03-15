/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/routes/McRouteHandleProvider.h"

#include "mcrouter/routes/ModifyExptimeRoute.h"
#include "mcrouter/routes/ModifyKeyRoute.h"
#include "mcrouter/routes/OperationSelectorRoute.h"
#include "mcrouter/routes/OriginalClientHashRoute.h"
#include "mcrouter/routes/RandomRouteFactory.h"
#include "mcrouter/routes/RoutingGroupRoute.h"
#include "mcrouter/routes/ShadowRoute.h"
#include "mcrouter/routes/StagingRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

using McRouteHandleFactory = RouteHandleFactory<McrouterRouteHandleIf>;
using MemcacheRouterInfo = facebook::memcache::MemcacheRouterInfo;

McrouterRouteHandlePtr makeWarmUpRoute(
    McRouteHandleFactory& factory,
    const folly::dynamic& json);

template <>
void McRouteHandleProvider<MemcacheRouterInfo>::buildRouteMap3(
    RouteHandleFactoryMap& map) {
  map.insert(std::make_pair(
      "ModifyKeyRoute", &makeModifyKeyRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "ModifyExptimeRoute", &makeModifyExptimeRoute<MemcacheRouterInfo>));
  map.insert(
      std::make_pair("NullRoute", &makeNullRoute<MemcacheRouteHandleIf>));
  map.insert(std::make_pair(
      "OperationSelectorRoute",
      &makeOperationSelectorRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "OriginalClientHashRoute",
      &makeOriginalClientHashRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "PoolRoute",
      [this](McRouteHandleFactory& factory, const folly::dynamic& json) {
        return makePoolRoute(factory, json);
      }));
  map.insert(std::make_pair(
      "PrefixPolicyRoute", &makeOperationSelectorRoute<MemcacheRouterInfo>));
  map.insert(
      std::make_pair("RandomRoute", &makeRandomRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "RateLimitRoute",
      [](McRouteHandleFactory& factory, const folly::dynamic& json) {
        return makeRateLimitRoute(factory, json);
      }));
  map.insert(std::make_pair(
      "RoutingGroupRoute", &makeRoutingGroupRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair("StagingRoute", &makeStagingRoute));
  map.insert(std::make_pair(
      "SRRoute",
      [this](McRouteHandleFactory& factory, const folly::dynamic& json) {
        return createSRRoute(factory, json);
      }));
  map.insert(std::make_pair("WarmUpRoute", &makeWarmUpRoute));
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
