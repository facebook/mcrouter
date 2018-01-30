/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>

namespace facebook {
namespace memcache {
namespace mcrouter {

enum class ConfigType { ConfigFile = 0, ConfigImport = 1, Pool = 2 };

class ConfigApiIf {
 public:
  virtual bool
  get(ConfigType type, const std::string& path, std::string& contents) = 0;

  virtual bool getConfigFile(std::string& config, std::string& path) = 0;

  virtual ~ConfigApiIf() = default;
};
}
}
} // facebook::memcache::mcrouter
