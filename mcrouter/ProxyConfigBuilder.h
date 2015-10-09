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

#include <folly/dynamic.h>
#include <folly/Range.h>

#include "mcrouter/options.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ConfigApi;
class McrouterInstance;
class PoolFactory;
class ProxyConfig;
class proxy_t;

class ProxyConfigBuilder {
 public:
  ProxyConfigBuilder(const McrouterOptions& opts,
                     ConfigApi& configApi,
                     folly::StringPiece jsonC);

  std::shared_ptr<ProxyConfig> buildConfig(proxy_t& proxy) const;

  folly::dynamic preprocessedConfig() const;
 private:
  folly::dynamic json_;
  std::shared_ptr<PoolFactory> poolFactory_;
  std::string configMd5Digest_;
};

}}} // facebook::memcache::mcrouter
