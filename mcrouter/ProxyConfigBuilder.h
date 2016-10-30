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

#include <folly/dynamic.h>
#include <folly/Range.h>

#include "mcrouter/PoolFactory.h"
#include "mcrouter/options.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ConfigApi;
template <class RouteHandleIf>
class ProxyConfig;
class Proxy;

class ProxyConfigBuilder {
 public:
  ProxyConfigBuilder(const McrouterOptions& opts,
                     ConfigApi& configApi,
                     folly::StringPiece jsonC);

  template <class RouteHandleIf>
  std::shared_ptr<ProxyConfig<RouteHandleIf>> buildConfig(
      Proxy& proxy) const {
    return std::shared_ptr<ProxyConfig<RouteHandleIf>>(
        new ProxyConfig<RouteHandleIf>(
            proxy, json_, configMd5Digest_, *poolFactory_));
  }

  const folly::dynamic& preprocessedConfig() const {
    return json_;
  }
 private:
  folly::dynamic json_;
  std::unique_ptr<PoolFactory> poolFactory_;
  std::string configMd5Digest_;
};

}}} // facebook::memcache::mcrouter
