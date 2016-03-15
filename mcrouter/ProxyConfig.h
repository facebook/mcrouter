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

#include "mcrouter/routes/McrouterRouteHandle.h"

namespace folly {
struct dynamic;
} // folly

namespace facebook { namespace memcache { namespace mcrouter {

class PoolFactory;
class ProxyRoute;
class ServiceInfo;
struct proxy_t;

/**
 * Topmost struct for mcrouter configs.
 */
class ProxyConfig {
 public:
  ProxyRoute& proxyRoute() const {
    return *proxyRoute_;
  }

  std::shared_ptr<ServiceInfo> serviceInfo() const {
    return serviceInfo_;
  }

  std::string getConfigMd5Digest() const {
    return configMd5Digest_;
  }

  McrouterRouteHandlePtr
  getRouteHandleForAsyncLog(folly::StringPiece asyncLogName) const;

  const folly::StringKeyedUnorderedMap<std::vector<McrouterRouteHandlePtr>>&
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
  std::shared_ptr<ProxyRoute> proxyRoute_;
  std::shared_ptr<ServiceInfo> serviceInfo_;
  std::string configMd5Digest_;
  folly::StringKeyedUnorderedMap<McrouterRouteHandlePtr> asyncLogRoutes_;
  folly::StringKeyedUnorderedMap<std::vector<McrouterRouteHandlePtr>> pools_;
  folly::StringKeyedUnorderedMap<
    std::vector<std::shared_ptr<const AccessPoint>>
  > accessPoints_;

  /**
   * Parses config and creates ProxyRoute
   *
   * @param jsonC config in format of JSON with comments and templates
   */
  ProxyConfig(proxy_t& proxy,
              const folly::dynamic& json,
              std::string configMd5Digest,
              PoolFactory& poolFactory);

  friend class ProxyConfigBuilder;
};

}}} // facebook::memcache::mcrouter
