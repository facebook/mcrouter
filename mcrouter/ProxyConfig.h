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

#include <memory>
#include <string>

#include <folly/Range.h>
#include <folly/experimental/StringKeyedUnorderedMap.h>

namespace folly {
struct dynamic;
} // folly

namespace facebook {
namespace memcache {

struct AccessPoint;

namespace mcrouter {

template <class RouterInfo>
class Proxy;
template <class RouterInfo>
class ProxyRoute;
template <class RouterInfo>
class ServiceInfo;

class PoolFactory;

/**
 * Topmost struct for mcrouter configs.
 */
template <class RouterInfo>
class ProxyConfig {
 public:
  ProxyRoute<RouterInfo>& proxyRoute() const {
    return *proxyRoute_;
  }

  std::shared_ptr<ServiceInfo<RouterInfo>> serviceInfo() const {
    return serviceInfo_;
  }

  std::string getConfigMd5Digest() const {
    return configMd5Digest_;
  }

  std::shared_ptr<typename RouterInfo::RouteHandleIf> getRouteHandleForAsyncLog(
      folly::StringPiece asyncLogName) const;

  const folly::StringKeyedUnorderedMap<
      std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>>>&
  getPools() const {
    return pools_;
  }

  const folly::StringKeyedUnorderedMap<
      std::vector<std::shared_ptr<const AccessPoint>>>&
  getAccessPoints() const {
    return accessPoints_;
  }

  size_t calcNumClients() const;

 private:
  // This map (accessPoints_) needs to be destroyed as the last object in the
  // config (after all RouteHandles) because its keys are being referenced
  // by object in the Config.
  folly::StringKeyedUnorderedMap<
      std::vector<std::shared_ptr<const AccessPoint>>>
      accessPoints_;
  folly::StringKeyedUnorderedMap<
      std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>>>
      pools_;
  std::shared_ptr<ProxyRoute<RouterInfo>> proxyRoute_;
  std::shared_ptr<ServiceInfo<RouterInfo>> serviceInfo_;
  std::string configMd5Digest_;
  folly::StringKeyedUnorderedMap<
      std::shared_ptr<typename RouterInfo::RouteHandleIf>>
      asyncLogRoutes_;

  /**
   * Parses config and creates ProxyRoute
   *
   * @param jsonC config in format of JSON with comments and templates
   */
  ProxyConfig(
      Proxy<RouterInfo>& proxy,
      const folly::dynamic& json,
      std::string configMd5Digest,
      PoolFactory& poolFactory);

  friend class ProxyConfigBuilder;
};
} // namespace mcrouter
} // namespace memcache
} // namespace facebook

#include "ProxyConfig-inl.h"
