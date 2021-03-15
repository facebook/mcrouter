/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "McRouteHandleProvider.h"

#include "mcrouter/lib/network/MessageHelpers.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/routes/McExtraRouteHandleProvider.h"

namespace folly {
struct dynamic;
}

namespace facebook {
namespace memcache {
namespace mcrouter {

using McRouteHandleFactory = RouteHandleFactory<McrouterRouteHandleIf>;
using MemcacheRouterInfo = facebook::memcache::MemcacheRouterInfo;

template <>
std::unique_ptr<ExtraRouteHandleProviderIf<MemcacheRouterInfo>>
McRouteHandleProvider<MemcacheRouterInfo>::buildExtraProvider() {
  return std::make_unique<McExtraRouteHandleProvider<MemcacheRouterInfo>>();
}

template <>
std::shared_ptr<MemcacheRouterInfo::RouteHandleIf>
McRouteHandleProvider<MemcacheRouterInfo>::createSRRoute(
    RouteHandleFactory<MemcacheRouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  if (makeSRRoute) {
    auto route = makeSRRoute(factory, json, proxy_);

    bool needAsynclog = true;
    if (json.isObject()) {
      if (auto* jNeedAsynclog = json.get_ptr("asynclog")) {
        needAsynclog = parseBool(*jNeedAsynclog, "asynclog");
      }

      if (needAsynclog) {
        auto jAsynclogName = json.get_ptr("service_name");
        checkLogic(
            jAsynclogName != nullptr,
            "AsynclogRoute over SRRoute: 'service_name' property is missing");
        auto asynclogName = parseString(*jAsynclogName, "service_name");
        return createAsynclogRoute(std::move(route), asynclogName.toString());
      }
      return route;
    }
  }

  throwLogic("SRRoute is not implemented for this router");
}

template <>
typename McRouteHandleProvider<MemcacheRouterInfo>::RouteHandleFactoryMap
McRouteHandleProvider<MemcacheRouterInfo>::buildRouteMap() {
  RouteHandleFactoryMap map;
  buildRouteMap0(map);
  buildRouteMap1(map);
  buildRouteMap2(map);
  buildRouteMap3(map);

  return map;
}

template <>
typename McRouteHandleProvider<
    MemcacheRouterInfo>::RouteHandleFactoryMapWithProxy
McRouteHandleProvider<MemcacheRouterInfo>::buildRouteMapWithProxy() {
  return RouteHandleFactoryMapWithProxy();
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
