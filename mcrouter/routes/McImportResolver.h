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

#include <string>

#include <folly/Range.h>

#include "mcrouter/lib/config/ImportResolverIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ConfigApi;

/**
 * ImportResolverIf implementation. Can load config files for
 * @import macro from configerator/file
 */
class McImportResolver : public ImportResolverIf {
 public:
  explicit McImportResolver(ConfigApi& configApi);

  /**
   * @throws std::runtime_error if can not load file
   */
  std::string import(folly::StringPiece path);
 private:
  ConfigApi& configApi_;
};

}}} // facebook::memcache::mcrouter
