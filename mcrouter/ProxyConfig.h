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

#include <memory>
#include <string>
#include <unordered_map>

#include <folly/Range.h>

#include "mcrouter/routes/McrouterRouteHandle.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

class PoolFactory;
class ProxyClientCommon;
class ProxyGenericPool;
class ProxyRoute;
class ServiceInfo;
class proxy_t;

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

  const std::vector<std::shared_ptr<const ProxyClientCommon>>&
  getClients() const;

  std::string getConfigMd5Digest() const {
    return configMd5Digest_;
  }

  McrouterRouteHandlePtr
  getRouteHandleForAsyncLog(const std::string& asyncLogName) const;

 private:
  std::shared_ptr<ProxyRoute> proxyRoute_;
  std::shared_ptr<ServiceInfo> serviceInfo_;
  std::shared_ptr<PoolFactory> poolFactory_;
  std::string configMd5Digest_;
  std::unordered_map<std::string, McrouterRouteHandlePtr> asyncLogRoutes_;

  /**
   * Parses config and creates ProxyRoute
   *
   * @param jsonC config in format of JSON with comments and templates
   */
  ProxyConfig(proxy_t& proxy,
              const folly::dynamic& json,
              std::string configMd5Digest,
              std::shared_ptr<PoolFactory> poolFactory);

  friend class ProxyConfigBuilder;
};

}}} // facebook::memcache::mcrouter
