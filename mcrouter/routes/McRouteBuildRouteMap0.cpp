/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/routes/McRouteHandleProvider.h"

#include "mcrouter/routes/AllAsyncRouteFactory.h"
#include "mcrouter/routes/AllFastestRouteFactory.h"
#include "mcrouter/routes/AllInitialRouteFactory.h"
#include "mcrouter/routes/AllMajorityRouteFactory.h"
#include "mcrouter/routes/AllSyncRouteFactory.h"
#include "mcrouter/routes/BlackholeRoute.h"
#include "mcrouter/routes/CarbonLookasideRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

using McRouteHandleFactory = RouteHandleFactory<McrouterRouteHandleIf>;
using MemcacheRouterInfo = facebook::memcache::MemcacheRouterInfo;

/**
 * This implementation is only for test purposes. Typically the users of
 * CarbonLookaside will be services other than memcache.
 */
class MemcacheCarbonLookasideHelper {
 public:
  MemcacheCarbonLookasideHelper(const folly::dynamic* /* jsonConfig */) {}

  static std::string name() {
    return "MemcacheCarbonLookasideHelper";
  }

  template <typename Request>
  bool cacheCandidate(const Request& /* unused */) const {
    if (HasKeyTrait<Request>::value) {
      return true;
    }
    return false;
  }

  template <typename Request>
  std::string buildKey(const Request& req) const {
    if (HasKeyTrait<Request>::value) {
      return req.key_ref()->fullKey().str();
    }
    return std::string();
  }

  template <typename Reply>
  bool shouldCacheReply(const Reply& /* unused */) const {
    return true;
  }

  template <typename Reply>
  void postProcessCachedReply(Reply& /* reply */) const {}
};

template <>
void McRouteHandleProvider<MemcacheRouterInfo>::buildRouteMap0(
    RouteHandleFactoryMap& map) {
  map.insert(
      std::make_pair("AllAsyncRoute", &makeAllAsyncRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "AllFastestRoute", &makeAllFastestRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "AllInitialRoute", &makeAllInitialRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "AllMajorityRoute", &makeAllMajorityRoute<MemcacheRouterInfo>));
  map.insert(
      std::make_pair("AllSyncRoute", &makeAllSyncRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "BlackholeRoute", &makeBlackholeRoute<MemcacheRouterInfo>));
  map.insert(std::make_pair(
      "CarbonLookasideRoute",
      &createCarbonLookasideRoute<
          MemcacheRouterInfo,
          MemcacheCarbonLookasideHelper>));
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
