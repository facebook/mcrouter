/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyConfigBuilder.h"

#include <folly/json.h>

#include "mcrouter/ConfigApi.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/config/ConfigPreprocessor.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/routes/McImportResolver.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

ProxyConfigBuilder::ProxyConfigBuilder(
    const McrouterOptions& opts,
    ConfigApi& configApi,
    folly::StringPiece jsonC)
    : json_(nullptr) {
  McImportResolver importResolver(configApi);
  folly::StringKeyedUnorderedMap<folly::dynamic> globalParams{
      {"default-route", opts.default_route.str()},
      {"default-region", opts.default_route.getRegion().str()},
      {"default-cluster", opts.default_route.getCluster().str()},
      {"hostid", globals::hostid()},
      {"router-name", opts.router_name},
      {"service-name", opts.service_name}};
  auto additionalParams = additionalConfigParams();
  for (auto& it : additionalParams) {
    globalParams.emplace(it.first, std::move(it.second));
  }
  for (const auto& param : opts.config_params) {
    globalParams.emplace(param.first, param.second);
  }

  json_ = ConfigPreprocessor::getConfigWithoutMacros(
      jsonC, importResolver, std::move(globalParams));

  poolFactory_ = std::make_unique<PoolFactory>(json_, configApi);

  configMd5Digest_ = Md5Hash(jsonC);
}
}
}
} // facebook::memcache::mcrouter
