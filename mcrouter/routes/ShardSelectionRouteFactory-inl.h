/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/Range.h>
#include <folly/dynamic.h>

#include "mcrouter/lib/SelectionRouteFactory.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/ErrorRoute.h"
#include "mcrouter/routes/LatestRoute.h"
#include "mcrouter/routes/LoadBalancerRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

const folly::dynamic& getPoolJson(const folly::dynamic& json);

const folly::dynamic& getShardsJson(const folly::dynamic& json);

void parseShardsPerServerJson(
    const folly::dynamic& json,
    std::function<void(uint32_t)>&& f);

size_t getMaxShardId(const std::vector<std::vector<size_t>>& allShards);

std::vector<std::vector<size_t>> parseAllShardsJson(
    const folly::dynamic& allShardsJson);

template <class MapType>
MapType prepareMap(size_t shardsMapSize);
template <>
inline std::vector<uint16_t> prepareMap(size_t shardsMapSize) {
  return std::vector<uint16_t>(
      shardsMapSize, std::numeric_limits<uint16_t>::max());
}
template <>
inline std::unordered_map<uint32_t, uint32_t> prepareMap(
    size_t /* shardsMapSize */) {
  return std::unordered_map<uint32_t, uint32_t>();
}

inline bool containsShard(const std::vector<uint16_t>& vec, size_t shard) {
  return vec.at(shard) != std::numeric_limits<uint16_t>::max();
}
inline bool containsShard(
    const std::unordered_map<uint32_t, uint32_t>& map,
    size_t shard) {
  return (map.find(shard) != map.end());
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

  size_t shardsMapSize = getMaxShardId(allShards) + 1;
  auto shardsMap = prepareMap<MapType>(shardsMapSize);

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
    const folly::dynamic& json,
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory) {
  const auto jPools = [&json]() {
    auto poolsJson = json.get_ptr("pools");
    checkLogic(
        poolsJson && poolsJson->isArray(),
        "EagerShardSelectionRoute: 'pools' not found");
    return *poolsJson;
  }();
  ShardDestinationsMap<RouterInfo> shardMap;

  for (const auto& jPool : jPools) {
    auto poolJson = getPoolJson(jPool);
    auto destinations = factory.createList(poolJson);
    auto shardsJson = getShardsJson(jPool);
    checkLogic(
        shardsJson.size() == destinations.size(),
        "EagerShardSelectionRoute: 'shards' must have the same number of "
        "entries as servers in 'pool'");
    if (destinations.empty()) {
      continue;
    }

    for (size_t j = 0; j < shardsJson.size(); j++) {
      parseShardsPerServerJson(
          shardsJson[j], [&destinations, &shardMap, j](uint32_t shard) {
            auto rh = destinations[j];
            auto it = shardMap.find(shard);
            if (it == shardMap.end()) {
              it = shardMap
                       .insert(
                           {shard,
                            std::vector<typename RouterInfo::RouteHandlePtr>()})
                       .first;
            }
            it->second.push_back(std::move(rh));
          });
    }
  }
  return shardMap;
}

} // namespace detail

// routes
template <class RouterInfo, class ShardSelector, class MapType>
typename RouterInfo::RouteHandlePtr createShardSelectionRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "ShardSelectionRoute config should be an object");

  auto poolJson = detail::getPoolJson(json);
  auto destinations = factory.createList(poolJson);
  if (destinations.empty()) {
    LOG(WARNING) << "ShardSelectionRoute: Empty list of destinations found. "
                 << "Using ErrorRoute.";
    return mcrouter::createErrorRoute<RouterInfo>(
        "ShardSelectionRoute has an empty list of destinations");
  }

  auto shardsJson = detail::getShardsJson(json);
  checkLogic(
      shardsJson.size() == destinations.size(),
      "ShardSelectionRoute: 'shards' must have the same number of "
      "entries as servers in 'pool'");

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
    const folly::dynamic& json) {
  checkLogic(
      json.isObject(), "EagerShardSelectionRoute config should be an object");

  auto shardMap = detail::getShardDestinationsMap<RouterInfo>(json, factory);
  if (shardMap.empty()) {
    return mcrouter::createErrorRoute<RouterInfo>(
        "EagerShardSelectionRoute has an empty list of destinations");
  }

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

  using CreateRouteFunc = typename RouterInfo::RouteHandlePtr (*)(
      RouteHandleFactory<typename RouterInfo::RouteHandleIf> & factory,
      const folly::dynamic&,
      std::vector<typename RouterInfo::RouteHandlePtr>);
  CreateRouteFunc createRouteFn;
  if (childrenType == "LoadBalancerRoute") {
    createRouteFn = createLoadBalancerRoute<RouterInfo>;
  } else if (childrenType == "LatestRoute") {
    createRouteFn = createLatestRoute<RouterInfo>;
  } else {
    throwLogic(
        "EagerShardSelectionRoute: 'children_type' {} not supported",
        childrenType);
  }

  MapType shardToDestinationIndexMap =
      detail::prepareMap<MapType>(shardMap.size());
  std::vector<typename RouterInfo::RouteHandlePtr> destinations;
  std::for_each(shardMap.begin(), shardMap.end(), [&](auto& item) {
    auto shardId = item.first;
    auto childrenRouteHandles = std::move(item.second);
    destinations.push_back(createRouteFn(
        factory, childrenSettings, std::move(childrenRouteHandles)));
    shardToDestinationIndexMap[shardId] = destinations.size() - 1;
  });

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
