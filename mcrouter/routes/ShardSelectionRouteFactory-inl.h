/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#pragma once

#include <folly/Range.h>
#include <folly/dynamic.h>

#include "mcrouter/lib/DynamicUtil.h"
#include "mcrouter/lib/SelectionRouteFactory.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/ErrorRoute.h"
#include "mcrouter/routes/LatestRoute.h"
#include "mcrouter/routes/LoadBalancerRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

std::vector<size_t> parseShardsPerServerJson(const folly::dynamic& json);

std::vector<std::vector<size_t>> parseAllShardsJson(
    const folly::dynamic& allShardsJson);

inline size_t getMaxShardId(const std::vector<std::vector<size_t>>& shardsMap) {
  size_t maxShardId = 0;
  for (const auto& shards : shardsMap) {
    for (auto shardId : shards) {
      maxShardId = std::max(maxShardId, shardId);
    }
  }
  return maxShardId;
}
template <class T>
size_t getMaxShardId(const std::unordered_map<uint32_t, T>& shardsMap) {
  uint32_t maxShardId = 0;
  for (const auto& item : shardsMap) {
    auto shardId = item.first;
    maxShardId = std::max(maxShardId, shardId);
  }
  return maxShardId;
}

template <class MapType>
MapType prepareMap(size_t numDistinctShards, size_t maxShardId);
template <>
inline std::vector<uint16_t> prepareMap(
    size_t /* numDistinctShards */,
    size_t maxShardId) {
  return std::vector<uint16_t>(
      maxShardId + 1, std::numeric_limits<uint16_t>::max());
}
template <>
inline std::unordered_map<uint32_t, uint32_t> prepareMap(
    size_t numDistinctShards,
    size_t /* maxShardId */) {
  return std::unordered_map<uint32_t, uint32_t>(numDistinctShards);
}

inline bool containsShard(const std::vector<uint16_t>& vec, size_t shard) {
  return vec.at(shard) != std::numeric_limits<uint16_t>::max();
}
inline bool containsShard(
    const std::unordered_map<uint32_t, uint32_t>& map,
    size_t shard) {
  return (map.find(shard) != map.end());
}

template <class RouterInfo>
const folly::dynamic& getPoolJson(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  assert(json.isObject());

  const auto* poolJson = json.get_ptr("pool");
  checkLogic(poolJson, "ShardSelectionRoute: 'pool' not found");
  return factory.parsePool(*poolJson);
}

template <class RouterInfo>
const folly::dynamic& getShardsJson(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  assert(json.isObject());

  // first, look for shards inside the pool.
  const auto& poolJson = getPoolJson<RouterInfo>(factory, json);
  const auto* shardsJson = poolJson.get_ptr("shards");

  // if not found, look outside.
  // TODO: kill this fallback logic when every client is at version >= 28
  if (!shardsJson) {
    shardsJson = json.get_ptr("shards");
  }
  checkLogic(
      shardsJson && shardsJson->isArray(),
      "ShardSelectionRoute: 'shards' not found or not an array");
  return *shardsJson;
}

/**
 * Build a map from shardId -> destinationId.
 */
template <class MapType>
MapType getShardsMap(const folly::dynamic& json, size_t numDestinations) {
  assert(json.isArray());

  checkLogic(
      numDestinations < std::numeric_limits<uint16_t>::max(),
      "ShardSelectionRoute: Only up to {} destinations are supported. "
      "Current number of destinations: {}",
      std::numeric_limits<uint16_t>::max() - 1,
      numDestinations);

  // Validate and get a list of shards.
  auto allShards = parseAllShardsJson(json);

  auto shardsMap =
      prepareMap<MapType>(allShards.size(), getMaxShardId(allShards));

  // We don't need to validate here, as it was validated before.
  for (size_t i = 0; i < allShards.size(); ++i) {
    for (size_t j = 0; j < allShards[i].size(); ++j) {
      size_t shard = allShards[i][j];
      if (!containsShard(shardsMap, shard)) {
        shardsMap[shard] = i;
      } else {
        // Shard is served by two destinations, picking one randomly
        if (folly::Random::oneIn(2)) {
          shardsMap[shard] = i;
        }
      }
    }
  }

  return shardsMap;
}

template <class RouterInfo>
using ShardDestinationsMap = std::
    unordered_map<uint32_t, std::vector<typename RouterInfo::RouteHandlePtr>>;

template <class RouterInfo>
ShardDestinationsMap<RouterInfo> getShardDestinationsMap(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  const auto jPools = [&json]() {
    auto poolsJson = json.get_ptr("pools");
    checkLogic(
        poolsJson && poolsJson->isArray(),
        "EagerShardSelectionRoute: 'pools' not found");
    return *poolsJson;
  }();
  ShardDestinationsMap<RouterInfo> shardMap;

  for (const auto& jPool : jPools) {
    auto poolJson = getPoolJson<RouterInfo>(factory, jPool);
    auto destinations = factory.createList(poolJson);
    auto shardsJson = getShardsJson<RouterInfo>(factory, jPool);
    checkLogic(
        shardsJson.size() == destinations.size(),
        folly::sformat(
            "EagerShardSelectionRoute: 'shards' must have the same number of "
            "entries as servers in 'pool'. Servers size: {}. Shards size: {}.",
            destinations.size(),
            shardsJson.size()));
    if (destinations.empty()) {
      continue;
    }

    for (size_t j = 0; j < shardsJson.size(); j++) {
      for (auto shard : parseShardsPerServerJson(shardsJson[j])) {
        auto rh = destinations[j];
        auto it = shardMap.find(shard);
        if (it == shardMap.end()) {
          it = shardMap
                   .insert({shard,
                            std::vector<typename RouterInfo::RouteHandlePtr>()})
                   .first;
        }
        it->second.push_back(std::move(rh));
      }
    }
  }
  return shardMap;
}

template <class RouterInfo, class MapType>
void buildChildrenLatestRoutes(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json,
    const ShardDestinationsMap<RouterInfo>& shardMap,
    std::vector<typename RouterInfo::RouteHandlePtr>& destinations,
    MapType& shardToDestinationIndexMap) {
  LatestRouteOptions options =
      parseLatestRouteJson(json, factory.getThreadId());
  std::for_each(shardMap.begin(), shardMap.end(), [&](auto& item) {
    auto shardId = item.first;
    auto childrenRouteHandles = std::move(item.second);
    size_t numChildren = childrenRouteHandles.size();
    destinations.push_back(createLatestRoute<RouterInfo>(
        json,
        std::move(childrenRouteHandles),
        options,
        std::vector<double>(numChildren, 1.0)));
    shardToDestinationIndexMap[shardId] = destinations.size() - 1;
  });
}

template <class RouterInfo, class MapType>
void buildChildrenLoadBalancerRoutes(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& /* factory */,
    const folly::dynamic& json,
    const ShardDestinationsMap<RouterInfo>& shardMap,
    std::vector<typename RouterInfo::RouteHandlePtr>& destinations,
    MapType& shardToDestinationIndexMap) {
  LoadBalancerRouteOptions<RouterInfo> options =
      parseLoadBalancerRouteJson<RouterInfo>(json);
  std::for_each(shardMap.begin(), shardMap.end(), [&](auto& item) {
    auto shardId = item.first;
    auto childrenRouteHandles = std::move(item.second);
    destinations.push_back(createLoadBalancerRoute<RouterInfo>(
        std::move(childrenRouteHandles), options));
    shardToDestinationIndexMap[shardId] = destinations.size() - 1;
  });
}

template <class RouterInfo, class MapType>
void buildChildrenCustomRoutesFromMap(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json,
    const ShardDestinationsMap<RouterInfo>& shardMap,
    const RouteHandleWithChildrenFactoryFn<RouterInfo>& createCustomRh,
    std::vector<typename RouterInfo::RouteHandlePtr>& destinations,
    MapType& shardToDestinationIndexMap) {
  std::for_each(shardMap.begin(), shardMap.end(), [&](auto& item) {
    auto shardId = item.first;
    auto childrenRouteHandles = std::move(item.second);
    destinations.push_back(
        createCustomRh(factory, json, std::move(childrenRouteHandles)));
    shardToDestinationIndexMap[shardId] = destinations.size() - 1;
  });
}

template <class RouterInfo, class MapType>
void buildChildrenCustomJsonmRoutes(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json,
    const ShardDestinationsMap<RouterInfo>& shardMap,
    std::vector<typename RouterInfo::RouteHandlePtr>& destinations,
    MapType& shardToDestinationIndexMap) {
  std::for_each(shardMap.begin(), shardMap.end(), [&](auto& item) {
    auto shardId = item.first;
    auto childrenList = std::move(item.second);
    // push children to factory. Factory will use when it sees "$children_list$"
    factory.pushChildrenList(std::move(childrenList));
    destinations.push_back(factory.create(json));
    factory.popChildrenList();
    shardToDestinationIndexMap[shardId] = destinations.size() - 1;
  });
}

} // namespace detail

// routes
template <class RouterInfo, class ShardSelector, class MapType>
typename RouterInfo::RouteHandlePtr createShardSelectionRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "ShardSelectionRoute config should be an object");

  auto poolJson = detail::getPoolJson<RouterInfo>(factory, json);
  auto destinations = factory.createList(poolJson);
  if (destinations.empty()) {
    LOG(WARNING) << "ShardSelectionRoute: Empty list of destinations found. "
                 << "Using ErrorRoute.";
    return mcrouter::createErrorRoute<RouterInfo>(
        "ShardSelectionRoute has an empty list of destinations");
  }

  const auto& shardsJson = detail::getShardsJson<RouterInfo>(factory, json);
  checkLogic(
      shardsJson.size() == destinations.size(),
      folly::sformat(
          "ShardSelectionRoute: 'shards' must have the same number of "
          "entries as servers in 'pool'. Servers size: {}. Shards size: {}.",
          destinations.size(),
          shardsJson.size()));

  auto selector = ShardSelector(
      detail::getShardsMap<MapType>(shardsJson, destinations.size()));

  typename RouterInfo::RouteHandlePtr outOfRangeDestination = nullptr;
  if (auto outOfRangeJson = json.get_ptr("out_of_range")) {
    outOfRangeDestination = factory.create(*outOfRangeJson);
  }

  return createSelectionRoute<RouterInfo, ShardSelector>(
      std::move(destinations),
      std::move(selector),
      std::move(outOfRangeDestination));
}

template <class RouterInfo, class ShardSelector, class MapType>
typename RouterInfo::RouteHandlePtr createEagerShardSelectionRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json,
    const ChildrenFactoryMap<RouterInfo>& childrenFactoryMap) {
  checkLogic(
      json.isObject(), "EagerShardSelectionRoute config should be an object");

  const auto childrenType = [&json]() {
    auto jChildType = json.get_ptr("children_type");
    checkLogic(
        jChildType && jChildType->isString(),
        "EagerShardSelectionRoute: 'children_type' not found or is not a "
        "string");
    return jChildType->stringPiece();
  }();

  const auto& childrenSettings = [&json]() {
    auto jSettings = json.get_ptr("children_settings");
    checkLogic(
        jSettings && jSettings->isObject(),
        "EagerShardSelectionRoute: 'children_settings' not found or not an "
        "object");
    return *jSettings;
  }();

  auto shardMap = detail::getShardDestinationsMap<RouterInfo>(factory, json);
  if (shardMap.empty()) {
    return mcrouter::createErrorRoute<RouterInfo>(
        "EagerShardSelectionRoute has an empty list of destinations");
  }

  MapType shardToDestinationIndexMap = detail::prepareMap<MapType>(
      shardMap.size(), detail::getMaxShardId(shardMap));
  std::vector<typename RouterInfo::RouteHandlePtr> destinations;
  if (childrenType == "LoadBalancerRoute") {
    detail::buildChildrenLoadBalancerRoutes<RouterInfo, MapType>(
        factory,
        childrenSettings,
        shardMap,
        destinations,
        shardToDestinationIndexMap);
  } else if (childrenType == "LatestRoute") {
    detail::buildChildrenLatestRoutes<RouterInfo, MapType>(
        factory,
        childrenSettings,
        shardMap,
        destinations,
        shardToDestinationIndexMap);
  } else if (childrenType == "CustomJsonmRoute") {
    detail::buildChildrenCustomJsonmRoutes<RouterInfo, MapType>(
        factory,
        childrenSettings,
        shardMap,
        destinations,
        shardToDestinationIndexMap);
  } else {
    auto it = childrenFactoryMap.find(childrenType.str());
    if (it != childrenFactoryMap.end()) {
      detail::buildChildrenCustomRoutesFromMap<RouterInfo, MapType>(
          factory,
          childrenSettings,
          shardMap,
          it->second,
          destinations,
          shardToDestinationIndexMap);
    } else {
      throwLogic(
          "EagerShardSelectionRoute: 'children_type' {} not supported",
          childrenType);
    }
  }

  ShardSelector selector(std::move(shardToDestinationIndexMap));

  typename RouterInfo::RouteHandlePtr outOfRangeDestination = nullptr;
  if (auto outOfRangeJson = json.get_ptr("out_of_range")) {
    outOfRangeDestination = factory.create(*outOfRangeJson);
  }

  return createSelectionRoute<RouterInfo, ShardSelector>(
      std::move(destinations),
      std::move(selector),
      std::move(outOfRangeDestination));
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
