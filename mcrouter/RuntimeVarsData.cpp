/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "RuntimeVarsData.h"

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

RuntimeVarsData::RuntimeVarsData(folly::StringPiece json) {
  auto data = parseJsonString(json);
  for (const auto& jiter : data.items()) {
    auto& key = jiter.first;
    auto& value = jiter.second;
    if (!key.isString()) {
      throw std::runtime_error("Bad config format, must have string keys");
    }
    configData_.emplace(key.asString(), value);
  }
}

folly::dynamic RuntimeVarsData::getVariableByName(
    const std::string& name) const {
  auto value = configData_.find(name);
  if (value == configData_.end()) {
    return nullptr;
  }
  return value->second;
}
}
}
} // facebook::memcache::mcrouter
