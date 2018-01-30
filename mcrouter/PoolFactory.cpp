/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "PoolFactory.h"

#include <folly/json.h>

#include "mcrouter/ConfigApi.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

PoolFactory::PoolFactory(const folly::dynamic& config, ConfigApiIf& configApi)
    : configApi_(configApi) {
  checkLogic(config.isObject(), "config is not an object");
  if (auto jpools = config.get_ptr("pools")) {
    checkLogic(jpools->isObject(), "config: 'pools' is not an object");

    for (const auto& it : jpools->items()) {
      pools_.emplace(
          it.first.stringPiece(), std::make_pair(it.second, PoolState::NEW));
    }
  }
}

PoolFactory::PoolJson PoolFactory::parseNamedPool(folly::StringPiece name) {
  auto existingIt = pools_.find(name);
  if (existingIt == pools_.end()) {
    // get the pool from ConfigApi
    std::string jsonStr;
    checkLogic(
        configApi_.get(ConfigType::Pool, name.str(), jsonStr),
        "Can not read pool: {}",
        name);
    existingIt =
        pools_
            .emplace(
                name,
                std::make_pair(parseJsonString(jsonStr), PoolState::PARSED))
            .first;
    return PoolJson(existingIt->first, existingIt->second.first);
  }

  name = existingIt->first;
  auto& json = existingIt->second.first;
  auto& state = existingIt->second.second;
  switch (state) {
    case PoolState::PARSED:
      return PoolJson(name, json);
    case PoolState::PARSING:
      throwLogic("Cycle in pool inheritance");
    case PoolState::NEW:
      state = PoolState::PARSING;
      break;
  }

  if (auto jInherit = json.get_ptr("inherit")) {
    checkLogic(jInherit->isString(), "Pool {}: inherit is not a string", name);
    auto& newJson = parseNamedPool(jInherit->stringPiece()).json;
    json.update_missing(newJson);
    json.erase("inherit");
  }
  state = PoolState::PARSED;
  return PoolJson(name, json);
}

PoolFactory::PoolJson PoolFactory::parsePool(const folly::dynamic& json) {
  checkLogic(
      json.isString() || json.isObject(),
      "Pool should be a string (name of pool) or an object");
  if (json.isString()) {
    return parseNamedPool(json.stringPiece());
  }
  auto jname = json.get_ptr("name");
  checkLogic(jname && jname->isString(), "Pool should have string 'name'");
  pools_.emplace(jname->stringPiece(), std::make_pair(json, PoolState::NEW));
  return parseNamedPool(jname->stringPiece());
}
}
}
} // facebook::memcache::mcrouter
