/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#pragma once

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <vector>

#include <folly/Range.h>
#include <folly/dynamic.h>

#include <mcrouter/lib/config/RouteHandleFactory.h>
#include <mcrouter/lib/fbi/cpp/util.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

// Alias for eager shard selector factory.
template <class RouterInfo>
using RouteHandleWithChildrenFactoryFn =
    std::function<typename RouterInfo::RouteHandlePtr(
        RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
        const folly::dynamic& json,
        std::vector<typename RouterInfo::RouteHandlePtr> children)>;
template <class RouterInfo>
using ChildrenFactoryMap = std::
    unordered_map<std::string, RouteHandleWithChildrenFactoryFn<RouterInfo>>;

/**
 * Create a route handle for sharding requests to servers.
 *
 * Sample json format:
 * {
 *   "pool": {
 *     "type": "pool",
 *     "name": "A",
 *     "servers": [
 *       "server1:12345",
 *       "server2:12345"
 *     ],
 *     "shards": [
 *       [1, 3, 6],
 *       [2, 4, 5]
 *     ]
 *   },
 *   "out_of_range": "ErrorRoute"
 * }
 *
 * Alternatively, "shards" can be an array of strings of comma-separated
 * shard ids. For example:
 * {
 *   "pool": {
 *     "type": "pool",
 *     "name": "A",
 *     "servers": [
 *       "server1:12345",
 *       "server2:12345"
 *     ],
 *     "shards": [
 *       "1,3,6",
 *       "2,4,5"
 *     ]
 *   },
 * }
 *
 *
 * NOTE:
 *  - "shards" and "servers" must have the same number of entries, in exactly
 *    the same order (e.g. shards[5] shows the shards processed by servers[5]).
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
    const folly::dynamic& json);

/**
 * Creates a route handle that sends requests to destinations based on the
 * shards map.
 * If there are multiple servers serving the same shard, these destinations
 * will be grouped together under the route handle specified by the
 * "children_type" property.
 *
 * Sample json format:
 * {
 *   "children_type": "RouteHandle",
 *   "children_settings" : { .. },
 *   "pools" : [
 *      {
 *        "pool": {
 *          "type": "pool",
 *          "name": "A",
 *          "servers": [
 *            "server1:12345",
 *            "server2:12345"
 *          ],
 *          "shards": [
 *            [1, 3, 6],
 *            [2, 4, 5]
 *          ]
 *        },
 *      }
 *   ],
 *   "out_of_range": "ErrorRoute"
 * }
 *
 * DETAILS:
 *  - "shards" can also be an array of strings of comma-separated shard ids,
 *    as stated in the documentation of createShardSelectionRoute() above.
 *
 *  - "children_type" the name of route handle that will group the servers that
 *    are responsible for a given shard. Can be: LoadBalancerRoute,
 *    LatestRoute, CustomJsonmRoute, or one of the names provided in the
 *    childrenFactoryMap. If CustomJsonmRoute is used, "children_settings"
 *    will have access to "$children_list$", which expands to the list of
 *    servers that serve a given shard. Check EagerShardSelectionRouteTest for
 *    more details.
 *
 *  - "children_settings" is the properties of the route handle specified
 *    in the "children_type" option.
 *
 *  - "pools" is an array of pools (or tiers).
 *    - "shards" and "servers" must have the same number of entries, in exactly
 *      the same order (e.g. shards[5] has the shards processed by servers[5]).
 *
 * @tparam ShardSelector Class responsible for selecting the shard responsible
 *                       for handling the request. The ShardSelector constructor
 *                       takes a shardsMap (unordered_map) that maps
 *                       shardId -> destinationIndex.
 * @tparam MapType       C++ type container that maps shardId -> destinationIdx
 *
 * @param factory             The route handle factory.
 * @param json                JSON object with the config of this route handle.
 * @param childrenFactoryMap  Map with factory functions of custom route handles
 *                            that are supported to group the destinations that
 *                            handle a given shard.
 *
 * @return  Pointer to the EagerShardSelectionRoute.
 */
template <
    class RouterInfo,
    class ShardSelector,
    class MapType = std::unordered_map<uint32_t, uint32_t>>
typename RouterInfo::RouteHandlePtr createEagerShardSelectionRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json,
    const ChildrenFactoryMap<RouterInfo>& childrenFactoryMap = {});

} // namespace mcrouter
} // namespace memcache
} // namespace facebook

#include "mcrouter/routes/ShardSelectionRouteFactory-inl.h"
