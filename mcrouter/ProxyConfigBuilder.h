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
#include <folly/dynamic.h>

#include "mcrouter/PoolFactory.h"
#include "mcrouter/options.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouterInfo>
class ProxyConfig;
template <class RouterInfo>
class Proxy;

class ConfigApi;

class ProxyConfigBuilder {
 public:
  ProxyConfigBuilder(
      const McrouterOptions& opts,
      ConfigApi& configApi,
      folly::StringPiece jsonC);

  template <class RouterInfo>
  std::shared_ptr<ProxyConfig<RouterInfo>> buildConfig(
      Proxy<RouterInfo>& proxy) const {
    return std::shared_ptr<ProxyConfig<RouterInfo>>(new ProxyConfig<RouterInfo>(
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
}
}
} // facebook::memcache::mcrouter
