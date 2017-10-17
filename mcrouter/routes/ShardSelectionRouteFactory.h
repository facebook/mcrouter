/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <algorithm>
#include <cassert>

#include <folly/dynamic.h>

#include <mcrouter/lib/SelectionRouteFactory.h>
#include <mcrouter/lib/config/RouteHandleFactory.h>
#include <mcrouter/lib/fbi/cpp/util.h>
#include <mcrouter/routes/ErrorRoute.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

const folly::dynamic& getPoolJson(const folly::dynamic& json);

const folly::dynamic& getShardsJson(const folly::dynamic& json);
/**
 * Build a map from shardId -> destinationId.
 */
template <class MapType>
MapType getShardsMap(const folly::dynamic& json, size_t numDestinations);

} // namespace detail

/**
 * Create a route handle for sharding requests to servers.
 *
 * Sample json format:
 * {
 *   "pool": "Pool|smc:blah.test.region.00",
 *   "shards": [
 *     [1, 3, 6],
 *     [2, 4, 5],
 *     ...
 *   ],
 *   "out_of_range": "ErrorRoute"
 * }
 *
 * Alternatively, "shards" can be an array of strings of comma-separated
 * shard ids. For example:
 * {
 *   "pool": "Pool|smc:blah.test.region.00",
 *   "shards": [
 *     "1,3,6",
 *     "2,4,5",
 *     ...
 *   ],
 * }
 *
 *
 * NOTE:
 *  - "shards" and "pool" must have the same number of entries, in exactly
 *    the same order (e.g. `shards[5]` shows the shards processed by
 *    `pool.servers[5]`).
 *
 * @tparam ShardSelector Class responsible for selecting the shard responsible
 *                       for handling the request. The ShardSelector constructor
 *                       accepts a MapType shardsMap that maps
 *                       shardId -> destinationId.
 * @tparam MapType       C++ type container that maps shardId -> destinationId.
 *
 * @param factory               RouteHandleFactory to create destinations.
 * @param json                  JSON object with RouteHandle representation.
 */
template <
    class RouterInfo,
    class ShardSelector,
    class MapType = std::vector<uint16_t>>
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

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
