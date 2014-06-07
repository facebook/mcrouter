#include "ProxyConfigBuilder.h"

#include "folly/json.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/ConfigApi.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/priorities.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/McImportResolver.h"
#include "mcrouter/lib/config/ConfigPreprocessor.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyConfigBuilder::ProxyConfigBuilder(const McrouterOptions& opts,
                                       std::string defaultRoute,
                                       std::string defaultRegion,
                                       std::string defaultCluster,
                                       ConfigApi* configApi,
                                       folly::StringPiece jsonC)
    : json_(nullptr) {

  McImportResolver importResolver(configApi);
  json_ = ConfigPreprocessor::getConfigWithoutMacros(
    jsonC,
    importResolver,
    {
      { "default-route", defaultRoute },
      { "default-region", defaultRegion },
      { "default-cluster", defaultCluster },
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
