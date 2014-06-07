#include "McImportResolver.h"

#include "mcrouter/ConfigApi.h"
#include "mcrouter/lib/config/ImportResolverIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

McImportResolver::McImportResolver(ConfigApi* configApi)
  : configApi_(configApi) {
  if (configApi_ == nullptr) {
    throw std::runtime_error("ConfigApi is null");
  }
}

std::string McImportResolver::import(folly::StringPiece path) {
  std::string ret;
  if (!configApi_->get(ConfigType::ConfigImport, path.str(), ret)) {
    throw std::runtime_error("Can not read " + path.str());
  }
  return ret;
}

}}} // facebook::memcache::mcrouter
