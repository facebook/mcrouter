/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <folly/Range.h>
#include <folly/container/F14Set.h>

#include "mcrouter/lib/fbi/cpp/LowerBoundPrefixMap.h"
#include "mcrouter/lib/fbi/cpp/Trie.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouteHandleIf>
class PrefixSelectorRoute;

/**
 * @brief This class precalculates targets (vector of route handles) we should
 *        route to for some set of PrefixSelectorRoutes.
 *
 * Sometimes we want to route to multiple clusters in one request.
 * This happens if routing prefix contains some '*'. Most common cases are
 * /star/star/ and /region/star/. On the other hand different clusters can have
 * same OperationSelectorRoutes and we don't want to route to same
 * OperationSelectorRoute twice.
 *
 * Before to achieve this we iterated through all clusters that match
 * given routing prefix and built a set of unique OperationSelectorRoutes
 * on each request. This class precalculates these sets for some routing prefix.
 *
 * To use this class one should:
 * 1) create RoutePolicyMap with PrefixSelectorRoutes for all clusters that
 *    match the routing prefix e.g. for /star/star/ these will be all clusters.
 *    Order is important: targets for corresponding cluster will be in same
 *    order as in clusters vector passed to constructor. So local clusters
 *    should be at the beggining of vector.
 * 2) use getTargetsForKey to get precalculated vector of targets.
 *    Complexity is O(min(longest key prefix in config, key length))
 */
template <class RouteHandleIf>
class RoutePolicyMapV2 {
 public:
  using SharedRoutePtr = std::shared_ptr<RouteHandleIf>;
  using SharedClusterPtr = std::shared_ptr<PrefixSelectorRoute<RouteHandleIf>>;

  explicit RoutePolicyMapV2(std::span<const SharedClusterPtr> clusters);

  std::span<const SharedRoutePtr> getTargetsForKey(std::string_view key) const {
    // has to be there by construction
    auto found = ut_.findPrefix(key);
    CHECK_NE(found, ut_.end());
    return *found;
  }

 private:
  static std::vector<SharedRoutePtr> populateRoutesForKey(
      std::string_view key,
      std::span<const SharedClusterPtr> clusters);

  using LBRouteMap = LowerBoundPrefixMap<std::vector<SharedRoutePtr>>;

  LBRouteMap ut_;
};

template <class RouteHandleIf>
class RoutePolicyMap {
 public:
  explicit RoutePolicyMap(
      const std::vector<std::shared_ptr<PrefixSelectorRoute<RouteHandleIf>>>&
          clusters);

  /**
   * @return vector of route handles that a request with given key should be
   *         forwarded to.
   */
  const std::vector<std::shared_ptr<RouteHandleIf>>& getTargetsForKey(
      folly::StringPiece key) const;

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> emptyV_;
  /**
   * This Trie contains targets for each key prefix. It is built like this:
   * 1) targets for empty string are wildcards.
   * 2) targets for string of length n+1 S[0..n] are targets for S[0..n-1] with
   *    OperationSelectorRoutes for key prefix == S[0..n] overridden.
   */
  Trie<std::vector<std::shared_ptr<RouteHandleIf>>> ut_;
};

template <class RouteHandleIf>
RoutePolicyMapV2<RouteHandleIf>::RoutePolicyMapV2(
    std::span<const std::shared_ptr<PrefixSelectorRoute<RouteHandleIf>>>
        clusters) {
  std::vector<std::shared_ptr<RouteHandleIf>> wildcards;
  wildcards.reserve(clusters.size());
  for (const auto& cluster : clusters) {
    wildcards.push_back(cluster->wildcard);
  }

  typename LBRouteMap::Builder builder;

  // not enough but it's not a problem
  builder.reserve(clusters.size());
  builder.insert({"", std::move(wildcards)});

  // Combining routes for each key
  folly::F14FastSet<std::string_view> seen;
  for (const auto& cluster : clusters) {
    for (const auto& [key, _] : cluster->policies) {
      if (seen.insert(key).second) {
        builder.insert({key, populateRoutesForKey(key, clusters)});
      }
    }
  }
}

template <class RouteHandleIf>
// static
auto RoutePolicyMapV2<RouteHandleIf>::populateRoutesForKey(
    std::string_view key,
    std::span<const SharedClusterPtr> clusters) -> std::vector<SharedRoutePtr> {
  std::vector<std::shared_ptr<RouteHandleIf>> res;
  res.reserve(clusters.size());

  folly::F14FastSet<const RouteHandleIf*> seen;
  seen.reserve(clusters.size());

  for (const auto& cluster : clusters) {
    auto found = cluster->policies.findPrefix(key);
    const SharedRoutePtr& ptr =
        found == cluster->policies.end() ? cluster->wildcard : found->second;
    if (ptr && seen.insert(ptr.get()).second) {
      res.push_back(ptr);
    }
  }
  return res;
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook

#include "RoutePolicyMap-inl.h"
