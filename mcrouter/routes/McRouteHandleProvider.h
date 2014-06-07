/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/lib/config/RouteHandleProvider.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

class ExtraRouteHandleProviderIf;
class PoolFactoryIf;
class ProxyClientCommon;
class ProxyDestinationMap;
class ProxyPool;
class proxy_t;

/**
 * RouteHandleProviderIf implementation that can create mcrouter-specific
 * routes.
 */
class McRouteHandleProvider :
  public RouteHandleProvider<McrouterRouteHandleIf> {
 public:
  McRouteHandleProvider(proxy_t* proxy,
                        ProxyDestinationMap& destinationMap,
                        PoolFactoryIf& poolFactory);

  std::vector<McrouterRouteHandlePtr>
  create(RouteHandleFactory<McrouterRouteHandleIf>& factory,
         const std::string& type, const folly::dynamic& json) override;

  McrouterRouteHandlePtr
  createHash(const std::string& funcType,
             const folly::dynamic& json,
             std::vector<McrouterRouteHandlePtr> children) override;

  /**
   * Returns PoolRoute by given pool name.
   *
   * @param poolName Name of ProxyPool
   * @return PoolRoute for given pool name or nullptr if not found
   */
  McrouterRouteHandlePtr getPoolHandle(const std::string& poolName) const;

  ~McRouteHandleProvider();

 private:
  proxy_t* proxy_;
  ProxyDestinationMap& destinationMap_;
  PoolFactoryIf& poolFactory_;
  std::unique_ptr<ExtraRouteHandleProviderIf> extraProvider_;
  /// ProxyPool -> vector of DestinationRoutes
  std::unordered_map<std::shared_ptr<const ProxyPool>,
                     std::vector<McrouterRouteHandlePtr>> poolHandles_;
  // ProxyClientCommon -> DestinationRoute
  std::unordered_map<std::shared_ptr<const ProxyClientCommon>,
                     McrouterRouteHandlePtr> destinationHandles_;

  /// poolName -> AsynclogRoute
  std::unordered_map<std::string, McrouterRouteHandlePtr> poolNameToHandles_;

  McrouterRouteHandlePtr
  makeDestinationHandle(std::shared_ptr<const ProxyClientCommon> client);

  std::vector<McrouterRouteHandlePtr>
  getDestinationHandlesForPool(std::shared_ptr<const ProxyPool> pool);

  std::vector<McrouterRouteHandlePtr> makePool(const folly::dynamic& json);

  McrouterRouteHandlePtr makePoolRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json);
};

}}} // facebook::memcache::mcrouter
