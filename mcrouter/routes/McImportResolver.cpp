/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McImportResolver.h"

#include "mcrouter/ConfigApi.h"
#include "mcrouter/lib/config/ImportResolverIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

McImportResolver::McImportResolver(ConfigApi& configApi)
  : configApi_(configApi) {
}

std::string McImportResolver::import(folly::StringPiece path) {
  std::string ret;
  if (!configApi_.get(ConfigType::ConfigImport, path.str(), ret)) {
    throw std::runtime_error("Can not read " + path.str());
  }
  return ret;
}

}}} // facebook::memcache::mcrouter
