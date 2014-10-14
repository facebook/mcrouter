/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ProxyConfigBuilder.h"

#include <folly/json.h>

#include "mcrouter/ConfigApi.h"
#include "mcrouter/lib/config/ConfigPreprocessor.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/priorities.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/routes/McImportResolver.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyConfigBuilder::ProxyConfigBuilder(const McrouterOptions& opts,
                                       ConfigApi* configApi,
                                       folly::StringPiece jsonC)
    : json_(nullptr) {

  McImportResolver importResolver(configApi);
  json_ = ConfigPreprocessor::getConfigWithoutMacros(
    jsonC,
    importResolver,
    {
      { "default-route", opts.default_route.str() },
      { "default-region", opts.default_route.getRegion().str() },
      { "default-cluster", opts.default_route.getCluster().str() },
    });

  poolFactory_ = std::make_shared<PoolFactory>(json_, configApi, opts);

  configMd5Digest_ = Md5Hash(jsonC);
}

folly::dynamic ProxyConfigBuilder::preprocessedConfig() const {
  return json_;
}

std::shared_ptr<ProxyConfig>
ProxyConfigBuilder::buildConfig(proxy_t* proxy) const {
  return std::shared_ptr<ProxyConfig>(
    new ProxyConfig(proxy, json_, configMd5Digest_, poolFactory_));
}

}}} // facebook::memcache::mcrouter
