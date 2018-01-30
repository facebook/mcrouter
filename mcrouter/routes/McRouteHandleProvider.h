/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <folly/Range.h>

#include "mcrouter/PoolFactory.h"
#include "mcrouter/lib/config/RouteHandleProviderIf.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace folly {
struct dynamic;
} // folly

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouteHandleIf>
class ExtraRouteHandleProviderIf;
class ProxyBase;

/**
 * RouteHandleProviderIf implementation that can create mcrouter-specific
 * routes.
 */
template <class RouterInfo>
class McRouteHandleProvider
    : public RouteHandleProviderIf<typename RouterInfo::RouteHandleIf> {
 public:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;
  using RouteHandlePtr = std::shared_ptr<RouteHandleIf>;
  using RouteHandleFactoryFunc = std::function<RouteHandlePtr(
      RouteHandleFactory<RouteHandleIf>&,
      const folly::dynamic&)>;
  using RouteHandleFactoryMap = std::
      unordered_map<folly::StringPiece, RouteHandleFactoryFunc, folly::Hash>;

  McRouteHandleProvider(ProxyBase& proxy, PoolFactory& poolFactory);

  std::vector<RouteHandlePtr> create(
      RouteHandleFactory<RouteHandleIf>& factory,
      folly::StringPiece type,
      const folly::dynamic& json) final;

  folly::StringKeyedUnorderedMap<RouteHandlePtr> releaseAsyncLogRoutes() {
    return std::move(asyncLogRoutes_);
  }

  folly::StringKeyedUnorderedMap<std::vector<RouteHandlePtr>> releasePools() {
    return std::move(pools_);
  }

  folly::StringKeyedUnorderedMap<
      std::vector<std::shared_ptr<const AccessPoint>>>
  releaseAccessPoints() {
    return std::move(accessPoints_);
  }

  ~McRouteHandleProvider() override;

 private:
  ProxyBase& proxy_;
  PoolFactory& poolFactory_;
  std::unique_ptr<ExtraRouteHandleProviderIf<RouterInfo>> extraProvider_;

  // poolName -> AsynclogRoute
  folly::StringKeyedUnorderedMap<RouteHandlePtr> asyncLogRoutes_;

  // poolName -> destinations
  folly::StringKeyedUnorderedMap<std::vector<RouteHandlePtr>> pools_;

  // poolName -> AccessPoints
  folly::StringKeyedUnorderedMap<
      std::vector<std::shared_ptr<const AccessPoint>>>
      accessPoints_;

  const RouteHandleFactoryMap routeMap_;

  const std::vector<RouteHandlePtr>& makePool(
      RouteHandleFactory<RouteHandleIf>& factory,
      const PoolFactory::PoolJson& json);

  RouteHandlePtr makePoolRoute(
      RouteHandleFactory<RouteHandleIf>& factory,
      const folly::dynamic& json);

  RouteHandlePtr createAsynclogRoute(
      RouteHandlePtr route,
      std::string asynclogName);

  RouteHandleFactoryMap buildRouteMap();

  // This can be removed when the buildRouteMap specialization for
  // MemcacheRouterInfo is removed.
  RouteHandleFactoryMap buildCheckedRouteMap();

  std::unique_ptr<ExtraRouteHandleProviderIf<RouterInfo>> buildExtraProvider();
};

} // mcrouter
} // memcache
} // facebook

#include "McRouteHandleProvider-inl.h"
