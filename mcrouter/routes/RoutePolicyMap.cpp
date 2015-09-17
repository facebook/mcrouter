/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "RoutePolicyMap.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "mcrouter/routes/PrefixSelectorRoute.h"

using std::pair;
using std::vector;

namespace facebook { namespace memcache { namespace mcrouter {

static vector<McrouterRouteHandlePtr>
orderedUnique(const vector<McrouterRouteHandlePtr>& v) {
  std::unordered_set<McrouterRouteHandlePtr> seen;
  vector<McrouterRouteHandlePtr> result;
  for (auto& el : v) {
    if (el && seen.find(el) == seen.end()) {
      seen.insert(el);
      result.push_back(el);
    }
  }
  result.shrink_to_fit();
  return result;
}

static vector<McrouterRouteHandlePtr>
overrideItems(vector<McrouterRouteHandlePtr> original,
              const vector<pair<size_t, McrouterRouteHandlePtr>>& overrides) {
  for (auto& it : overrides) {
    original[it.first] = it.second;
  }
  return original;
}

RoutePolicyMap::RoutePolicyMap(
  const vector<std::shared_ptr<PrefixSelectorRoute>>& clusters) {
  // wildcards of all clusters
  vector<McrouterRouteHandlePtr> wildcards;
  // Trie with aggregated policies from all clusters
  Trie<vector<pair<size_t, McrouterRouteHandlePtr>>> t;

  for (size_t clusterId = 0; clusterId < clusters.size(); ++clusterId) {
    auto& policy = clusters[clusterId];
    wildcards.push_back(policy->wildcard);

    for (auto& it : policy->policies) {
      auto clusterHandlePair = std::make_pair(clusterId, it.second);
      auto existing = t.find(it.first);
      if (existing != t.end()) {
        existing->second.push_back(std::move(clusterHandlePair));
      } else {
        t.emplace(it.first, { clusterHandlePair });
      }
    }
  }

  ut_.emplace("", std::move(wildcards));
  // we iterate over keys in lexicographic order, so all prefixes of key will go
  // before key itself
  for (auto& it : t) {
    auto existing = ut_.findPrefix(it.first);
    // at least empty string should be there
    assert(existing != ut_.end());
    ut_.emplace(it.first, overrideItems(existing->second, it.second));
  }
  for (auto& it : ut_) {
    it.second = orderedUnique(it.second);
  }
}

const vector<McrouterRouteHandlePtr>&
RoutePolicyMap::getTargetsForKey(folly::StringPiece key) const {
  auto result = ut_.findPrefix(key);
  return result == ut_.end() ? emptyV_ : result->second;
}

}}}  // facebook::memcache::mcrouter
