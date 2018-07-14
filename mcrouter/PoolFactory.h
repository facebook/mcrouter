/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <folly/dynamic.h>
#include <folly/experimental/StringKeyedUnorderedMap.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

class ConfigApiIf;

/**
 * Parses mcrouter pools from mcrouter config.
 */
class PoolFactory {
 public:
  struct PoolJson {
    PoolJson(folly::StringPiece name_, const folly::dynamic& json_)
        : name(name_), json(json_) {}

    const folly::StringPiece name;
    const folly::dynamic& json;
  };

  /**
   * @param config JSON object with clusters/pools properties (both optional).
   * @param configApi API to fetch pools from files. Should be
   *                  reference once we'll remove 'routerless' mode.
   */
  PoolFactory(const folly::dynamic& config, ConfigApiIf& configApi);

  /**
   * Loads a pool from ConfigApi, expand `inherit`, etc.
   *
   * @param json pool json
   *
   * @return  object with pool name and final json blob.
   */
  PoolJson parsePool(const folly::dynamic& json);

 private:
  enum class PoolState { NEW, PARSING, PARSED };
  folly::StringKeyedUnorderedMap<std::pair<folly::dynamic, PoolState>> pools_;
  ConfigApiIf& configApi_;

  PoolJson parseNamedPool(folly::StringPiece name);
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
