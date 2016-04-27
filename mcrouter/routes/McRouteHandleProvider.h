/*
 *  Copyright (c) 2016, Facebook, Inc.
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

#include "mcrouter/lib/config/RouteHandleProviderIf.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace folly {
struct dynamic;
} // folly

namespace facebook { namespace memcache { namespace mcrouter {

class ExtraRouteHandleProviderIf;
struct proxy_t;

/**
 * RouteHandleProviderIf implementation that can create mcrouter-specific
 * routes.
 */
class McRouteHandleProvider :
  public RouteHandleProviderIf<McrouterRouteHandleIf> {
 public:
  McRouteHandleProvider(proxy_t& proxy, PoolFactory& poolFactory);

  std::vector<McrouterRouteHandlePtr>
  create(RouteHandleFactory<McrouterRouteHandleIf>& factory,
         folly::StringPiece type, const folly::dynamic& json) override final;

  folly::StringKeyedUnorderedMap<McrouterRouteHandlePtr>
  releaseAsyncLogRoutes() {
    return std::move(asyncLogRoutes_);
  }

  folly::StringKeyedUnorderedMap<std::vector<McrouterRouteHandlePtr>>
  releasePools() {
    return std::move(pools_);
  }

  folly::StringKeyedUnorderedMap<
    std::vector<std::shared_ptr<const AccessPoint>>>
  releaseAccessPoints() {
    return std::move(accessPoints_);
  }

  ~McRouteHandleProvider();

 private:
  using RouteFunc = std::function<
      McrouterRouteHandlePtr(RouteHandleFactory<McrouterRouteHandleIf>&,
                             const folly::dynamic&)>;
  proxy_t& proxy_;
  PoolFactory& poolFactory_;
  std::unique_ptr<ExtraRouteHandleProviderIf> extraProvider_;

  // poolName -> AsynclogRoute
  folly::StringKeyedUnorderedMap<McrouterRouteHandlePtr> asyncLogRoutes_;

  // poolName -> destinations
  folly::StringKeyedUnorderedMap<std::vector<McrouterRouteHandlePtr>> pools_;

  // poolName -> AccessPoints
  folly::StringKeyedUnorderedMap<
    std::vector<std::shared_ptr<const AccessPoint>>
  > accessPoints_;

  const std::unordered_map<folly::StringPiece, RouteFunc,
                           folly::StringPieceHash> routeMap_;

  const std::vector<McrouterRouteHandlePtr>&
  makePool(RouteHandleFactory<McrouterRouteHandleIf>& factory,
           const PoolFactory::PoolJson& json);

  McrouterRouteHandlePtr
  makePoolRoute(RouteHandleFactory<McrouterRouteHandleIf>& factory,
                const folly::dynamic& json);

  McrouterRouteHandlePtr
  createAsynclogRoute(McrouterRouteHandlePtr route, std::string asynclogName);
};

}}} // facebook::memcache::mcrouter
