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

#include <memory>
#include <string>

#include <folly/experimental/StringKeyedUnorderedMap.h>
#include <folly/Range.h>

namespace folly {
struct dynamic;
} // folly

namespace facebook { namespace memcache { namespace mcrouter {

template <class RouteHandleIf>
class ProxyRoute;
template <class RouteHandleIf>
class ServiceInfo;

class PoolFactory;
class Proxy;

/**
 * Topmost struct for mcrouter configs.
 */
template <class RouteHandleIf>
class ProxyConfig {
 public:
  ProxyRoute<RouteHandleIf>& proxyRoute() const {
    return *proxyRoute_;
  }

  std::shared_ptr<ServiceInfo<RouteHandleIf>> serviceInfo() const {
    return serviceInfo_;
  }

  std::string getConfigMd5Digest() const {
    return configMd5Digest_;
  }

  std::shared_ptr<RouteHandleIf> getRouteHandleForAsyncLog(
      folly::StringPiece asyncLogName) const;

  const folly::StringKeyedUnorderedMap<
      std::vector<std::shared_ptr<RouteHandleIf>>>&
  getPools() const {
    return pools_;
  }

  const folly::StringKeyedUnorderedMap<
    std::vector<std::shared_ptr<const AccessPoint>>
  >& getAccessPoints() const {
    return accessPoints_;
  }

  size_t calcNumClients() const;

 private:
  std::shared_ptr<ProxyRoute<RouteHandleIf>> proxyRoute_;
  std::shared_ptr<ServiceInfo<RouteHandleIf>> serviceInfo_;
  std::string configMd5Digest_;
  folly::StringKeyedUnorderedMap<std::shared_ptr<RouteHandleIf>>
      asyncLogRoutes_;
  folly::StringKeyedUnorderedMap<std::vector<std::shared_ptr<RouteHandleIf>>>
      pools_;
  folly::StringKeyedUnorderedMap<
    std::vector<std::shared_ptr<const AccessPoint>>
  > accessPoints_;

  /**
   * Parses config and creates ProxyRoute
   *
   * @param jsonC config in format of JSON with comments and templates
   */
  ProxyConfig(
      Proxy& proxy,
      const folly::dynamic& json,
      std::string configMd5Digest,
      PoolFactory& poolFactory);

  friend class ProxyConfigBuilder;
};

}}} // facebook::memcache::mcrouter

#include "ProxyConfig-inl.h"
