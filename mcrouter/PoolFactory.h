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

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

class McrouterOptions;

namespace mcrouter {

class ClientPool;
class ConfigApi;
class ProxyClientCommon;

/**
 * Parses mcrouter pools from mcrouter config.
 */
class PoolFactory {
 public:
  /**
   * @param config JSON object with clusters/pools properties (both optional).
   * @param configApi API to fetch pools from files. Should be
   *                  reference once we'll remove 'routerless' mode.
   * @param mcOpts mcrouter options for parsing.
   */
  PoolFactory(const folly::dynamic& config, ConfigApi& configApi,
              const McrouterOptions& opts);

  /**
   * Parses a single pool from given json blob.
   *
   * @param jpool should be the pool object.
   */
  std::shared_ptr<ClientPool> parsePool(const folly::dynamic& jpool);

  /**
   * @return All clients created.
   */
  const std::vector<std::shared_ptr<const ProxyClientCommon>>& clients() const {
    return clients_;
  }

 private:
  std::unordered_map<std::string, std::shared_ptr<ClientPool>> pools_;
  std::vector<std::shared_ptr<const ProxyClientCommon>> clients_;
  ConfigApi& configApi_;
  const McrouterOptions& opts_;

  std::shared_ptr<ClientPool>
  parsePool(const std::string& name, const folly::dynamic& jpool);

  void parseQos(std::string parentName, const folly::dynamic& jQos,
                uint64_t& qosClass, uint64_t& qosPath);
};

}}} // facebook::memcache::mcrouter
