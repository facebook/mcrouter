/*
 *  Copyright (c) 2015, Facebook, Inc.
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
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

class ClientPool;
class ExtraRouteHandleProviderIf;
class PoolFactory;
class proxy_t;

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
         folly::StringPiece type, const folly::dynamic& json) override;

  std::unordered_map<std::string, McrouterRouteHandlePtr>
  releaseAsyncLogRoutes() {
    return std::move(asyncLogRoutes_);
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
  std::unordered_map<std::string, McrouterRouteHandlePtr> asyncLogRoutes_;

  const std::unordered_map<folly::StringPiece, RouteFunc,
                           folly::StringPieceHash> routeMap_;

  std::pair<std::shared_ptr<ClientPool>, std::vector<McrouterRouteHandlePtr>>
  makePool(const folly::dynamic& json);

  McrouterRouteHandlePtr makePoolRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json);

  McrouterRouteHandlePtr
  createAsynclogRoute(McrouterRouteHandlePtr route, std::string asynclogName);
};

}}} // facebook::memcache::mcrouter
