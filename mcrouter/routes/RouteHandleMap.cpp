/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "RouteHandleMap.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "folly/Hash.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/proxy.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/PrefixRouteSelector.h"
#include "mcrouter/routes/RoutePolicyMap.h"
#include "mcrouter/routes/RouteSelectorMap.h"
#include "mcrouter/lib/fibers/FiberManager.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

typedef std::vector<std::shared_ptr<PrefixRouteSelector>> RouteSelectorVector;

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
  std::string defaultRoute,
  bool sendInvalidRouteToDefault)
    : defaultRoute_(std::move(defaultRoute)),
      sendInvalidRouteToDefault_(sendInvalidRouteToDefault) {

  checkLogic(routeSelectors.find(defaultRoute_) != routeSelectors.end(),
             "invalid default route: {}", defaultRoute_);

  RouteSelectorVector allRoutes;
  std::unordered_map<std::string, RouteSelectorVector> byRegion;
  std::unordered_map<std::string, RouteSelectorVector> byRoute;
  // add defaults first
  auto defaultRegion = getRegionFromRoutingPrefix(defaultRoute);
  for (const auto& it : routeSelectors) {
    if (it.first == defaultRoute_) {
      allRoutes.push_back(it.second);
    }

    auto region = getRegionFromRoutingPrefix(it.first);
    if (region == defaultRegion) {
      byRegion[region.str()].push_back(it.second);
    }
  }

  // then add rest
  for (const auto& it : routeSelectors) {
    if (it.first != defaultRoute_) {
      allRoutes.push_back(it.second);
    }

    auto region = getRegionFromRoutingPrefix(it.first);
    if (!region.empty() && region != defaultRoute_) {
      byRegion[region.str()].push_back(it.second);
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
    if (it.first != defaultRoute_ &&
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

  return fiber::runInMainContext(
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
}

const std::vector<McrouterRouteHandlePtr>&
RouteHandleMap::getBySingleRoute(folly::StringPiece route,
                                 folly::StringPiece key) const {
  auto it = byRoute_.find(route);
  if (it == byRoute_.end()) {
    return emptyV_;
  }
  return it->second->getTargetsForKey(key);
}

const std::vector<McrouterRouteHandlePtr>*
RouteHandleMap::getTargetsForKeyFast(folly::StringPiece prefix,
                                     folly::StringPiece key) const {
  // empty prefix => route to default route
  if (prefix.empty()) {
    return &getBySingleRoute(defaultRoute_, key);
  }

  // route to all routes
  if (prefix == "/*/*/") {
    return &allRoutes_->getTargetsForKey(key);
  }

  if (prefix.find("*") == std::string::npos) {
    // no stars at all
    return &getBySingleRoute(prefix, key);
  }

  if (prefix.endsWith("/*/")) {
    auto region = getRegionFromRoutingPrefix(prefix);
    if (region.empty()) {
      return &emptyV_;
    }
    // route to all clusters of some region (/region/*/)
    if (region.find("*") == std::string::npos) {
      auto it = byRegion_.find(region);
      if (it == byRegion_.end()) {
        return &emptyV_;
      }
      return &it->second->getTargetsForKey(key);
    }
  }

  // no other types supported
  return nullptr;
}

std::vector<McrouterRouteHandlePtr> RouteHandleMap::getTargetsForKey(
  folly::StringPiece prefix,
  folly::StringPiece key) const {

  auto rhPtr = getTargetsForKeyFast(prefix, key);
  if (UNLIKELY(rhPtr == nullptr)) {
    auto rh = getTargetsForKeySlow(prefix, key);
    rhPtr = &rh;
  }
  if (UNLIKELY(sendInvalidRouteToDefault_ && rhPtr->empty())) {
    return getBySingleRoute(defaultRoute_, key);
  }
  return *rhPtr;
}

}}}  // facebook::memcache::mcrouter
