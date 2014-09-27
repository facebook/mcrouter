/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <sys/time.h>

#include <memory>
#include <string>
#include <unordered_map>

#include <folly/Range.h>

#include "mcrouter/PoolFactoryIf.h"

using timeval_t = struct timeval;

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

class McrouterOptions;

namespace mcrouter {

class AccessPoint;
class ConfigApi;
class ProxyClientCommon;
class ProxyGenericPool;

/**
 * Parses mcrouter pools from mcrouter config.
 */
class PoolFactory : public PoolFactoryIf {
 public:
  /**
   * @param config JSON object with clusters/pools properties (both optional).
   * @param configApi API to fetch pools from files. Should be
   *                  reference once we'll remove 'routerless' mode.
   * @param mcOpts mcrouter options for parsing.
   */
  PoolFactory(const folly::dynamic& config, ConfigApi* configApi,
              const McrouterOptions& mcOpts);

  /**
   * If pool was already parsed, returns pointer to this pool. Otherwise
   * tries to fetch and parse pool from ConfigApi.
   */
  std::shared_ptr<ProxyGenericPool> fetchPool(folly::StringPiece poolName);

  /**
   * Parses a single pool from given json blob.
   *
   * @param jpool should be the pool object.
   * @param jpools list of pools for server list link
   */
  std::shared_ptr<ProxyGenericPool>
  parsePool(const std::string& pool_name_str,
            const folly::dynamic& jpool,
            const folly::dynamic& jpools);

  /**
   * @return All pools parsed.
   */
  const std::unordered_map<std::string, std::shared_ptr<ProxyGenericPool>>&
  pools() const;

  /**
   * @return All clients created.
   */
  const std::unordered_map<std::string,
                           std::shared_ptr<const ProxyClientCommon>>&
  clients() const;

 private:

  struct Options {
    int rxpriority{0};
    int txpriority{0};
    timeval_t regional_pools_timeout{0};
    timeval_t cluster_pools_timeout{0};
    std::string region;
    std::string cluster;
    uint32_t cross_region_timeout_ms{0};
    uint32_t cross_cluster_timeout_ms{0};
    uint32_t within_cluster_timeout_ms{0};
  };

  std::unordered_map<std::string, std::shared_ptr<ProxyGenericPool>> pools_;
  std::unordered_map<std::string,
                     std::shared_ptr<const ProxyClientCommon>> clients_;

  ConfigApi* configApi_;

  Options opts_;

  int addPoolToConfig(std::shared_ptr<ProxyGenericPool> pool);

  std::string genProxyClientKey(const AccessPoint& ap);

  int parseClusters(const folly::dynamic& json);

  int parseMigratedPools(const folly::dynamic& json, bool is_regional);

  /**
   * Parse pools from JSON subtree.
   *
   * @return 0 on failure
   */
  int parsePools(const folly::dynamic& json, int is_regional);
};

}}} // facebook::memcache::mcrouter
