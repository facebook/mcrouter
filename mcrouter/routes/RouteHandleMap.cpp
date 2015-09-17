/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "RouteHandleMap.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <folly/Hash.h>
#include <folly/experimental/fibers/FiberManager.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/proxy.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/PrefixSelectorRoute.h"
#include "mcrouter/routes/RoutePolicyMap.h"
#include "mcrouter/routes/RouteSelectorMap.h"
#include "mcrouter/RoutingPrefix.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

using RouteSelectorVector = std::vector<std::shared_ptr<PrefixSelectorRoute>>;

struct VectorHash {
  size_t operator()(const RouteSelectorVector& v) const {
    size_t ret = 0;
    for (const auto& it : v) {
      ret = folly::hash::hash_combine(ret, it);
    }
    return ret;
  }
};

typedef std::unordered_map<RouteSelectorVector, std::shared_ptr<RoutePolicyMap>,
        VectorHash> UniqueVectorMap;

std::shared_ptr<RoutePolicyMap> makePolicyMap(UniqueVectorMap& uniqueVectors,
                                              const RouteSelectorVector& v) {

  auto it = uniqueVectors.find(v);
  if (it != uniqueVectors.end()) {
    return it->second;
  }
  return uniqueVectors[v] = std::make_shared<RoutePolicyMap>(v);
}

}  // anonymous namespace

RouteHandleMap::RouteHandleMap(
  const RouteSelectorMap& routeSelectors,
  const RoutingPrefix& defaultRoute,
  bool sendInvalidRouteToDefault)
    : defaultRoute_(defaultRoute),
      sendInvalidRouteToDefault_(sendInvalidRouteToDefault) {

  checkLogic(routeSelectors.find(defaultRoute_) != routeSelectors.end(),
             "invalid default route: {}", defaultRoute_.str());

  RouteSelectorVector allRoutes;
  folly::StringKeyedUnorderedMap<RouteSelectorVector> byRegion;
  folly::StringKeyedUnorderedMap<RouteSelectorVector> byRoute;
  // add defaults first
  for (const auto& it : routeSelectors) {
    RoutingPrefix prefix(it.first);
    if (prefix.str() == defaultRoute_.str()) {
      allRoutes.push_back(it.second);
    }

    if (prefix.getRegion() == defaultRoute_.getRegion()) {
      byRegion[prefix.getRegion()].push_back(it.second);
    }
  }

  // then add rest
  for (const auto& it : routeSelectors) {
    RoutingPrefix prefix(it.first);
    if (prefix.str() != defaultRoute_.str()) {
      allRoutes.push_back(it.second);
    }

    if (prefix.getRegion() != defaultRoute_.getRegion()) {
      byRegion[prefix.getRegion()].push_back(it.second);
    }

    byRoute[it.first].push_back(it.second);
  }

  // create corresponding RoutePolicyMaps
  UniqueVectorMap uniqueVectors;
  allRoutes_ = makePolicyMap(uniqueVectors, allRoutes);
  for (const auto& it : byRegion) {
    byRegion_.emplace(it.first, makePolicyMap(uniqueVectors, it.second));
  }
  for (const auto& it : byRoute) {
    byRoute_.emplace(it.first, makePolicyMap(uniqueVectors, it.second));
  }

  assert(byRoute_.find(defaultRoute_) != byRoute_.end());
  defaultRouteMap_ = byRoute_[defaultRoute_];
}

void RouteHandleMap::foreachRoutePolicy(folly::StringPiece prefix,
  std::function<void(const std::shared_ptr<RoutePolicyMap>&)> f) const {

  // if no route is provided or the default route matches the glob
  // then stick at the start so that we always send to the local cluster first
  if (prefix.empty() || match_pattern_route(prefix, defaultRoute_)) {
    auto it = byRoute_.find(defaultRoute_);
    if (it != byRoute_.end()) {
      f(it->second);
    }
  }

  if (prefix.empty()) {
    return;
  }

  bool selectAll = (prefix == "/*/*/");
  for (const auto& it : byRoute_) {
    if (it.first != defaultRoute_.str() &&
        (selectAll || match_pattern_route(prefix, it.first))) {
      f(it.second);
    }
  }
}

std::vector<McrouterRouteHandlePtr>
RouteHandleMap::getTargetsForKeySlow(folly::StringPiece prefix,
                                     folly::StringPiece key) const {
  struct Ctx {
    const RouteHandleMap* rhMap;
    // we need to ensure first policy is for local cluster
    McrouterRouteHandlePtr first;
    std::unordered_set<McrouterRouteHandlePtr> seen;
    Ctx(const RouteHandleMap* rhMap_) : rhMap(rhMap_) {}
  } c(this);

  auto result = folly::fibers::runInMainContext(
    [&c, prefix, key] () -> std::vector<McrouterRouteHandlePtr> {
      c.rhMap->foreachRoutePolicy(prefix,
        [&c, key] (const std::shared_ptr<RoutePolicyMap>& r) {
          const auto& policies = r->getTargetsForKey(key);
          for (const auto& policy : policies) {
            c.seen.insert(policy);
            if (!c.first) {
              c.first = policy;
            }
          }
        });

      if (!c.first) {
        return {};
      }

      std::vector<McrouterRouteHandlePtr> rh;
      rh.reserve(c.seen.size());
      rh.push_back(c.first);
      if (c.seen.size() > 1) {
        c.seen.erase(c.first);
        rh.insert(rh.end(), c.seen.begin(), c.seen.end());
      }
      return rh;
    }
  );
  if (result.empty() && sendInvalidRouteToDefault_) {
    return defaultRouteMap_->getTargetsForKey(key);
  }
  return result;
}

const std::vector<McrouterRouteHandlePtr>*
RouteHandleMap::getTargetsForKeyFast(folly::StringPiece prefix,
                                     folly::StringPiece key) const {
  const std::vector<McrouterRouteHandlePtr>* result = nullptr;
  if (prefix.empty()) {
    // empty prefix => route to default route
    result = &defaultRouteMap_->getTargetsForKey(key);
  } else if (prefix == "/*/*/") {
    // route to all routes
    result = &allRoutes_->getTargetsForKey(key);
  } else {
    auto starPos = prefix.find("*");
    if (starPos == std::string::npos) {
      // no stars at all
      auto it = byRoute_.find(prefix);
      result = it == byRoute_.end()
        ? &emptyV_
        : &it->second->getTargetsForKey(key);
    } else if (prefix.endsWith("/*/") && starPos == prefix.size() - 2) {
      // route to all clusters of some region (/region/*/)
      auto region = prefix.subpiece(1, prefix.size() - 4);
      auto it = byRegion_.find(region);
      result = it == byRegion_.end()
        ? &emptyV_
        : &it->second->getTargetsForKey(key);
    }
  }
  if (sendInvalidRouteToDefault_ && result != nullptr && result->empty()) {
    return &defaultRouteMap_->getTargetsForKey(key);
  }
  return result;
}

}}}  // facebook::memcache::mcrouter
