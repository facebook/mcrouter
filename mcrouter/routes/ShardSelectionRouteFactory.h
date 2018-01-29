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

#include <folly/Range.h>
#include <folly/dynamic.h>

#include <mcrouter/lib/config/RouteHandleFactory.h>
#include <mcrouter/lib/fbi/cpp/util.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

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
    const folly::dynamic& json);

/**
 * Create a route handle for sharding requests to servers with an additional
 * children_type route that is applied to destinations mapped to shards.
 *
 * Sample json format:
 * {
 *   "children_type": "RouteHandle",
 *   "pools" : [
 *   {
 *     "pool": "Pool|smc:blah.test.region.00",
 *     "shards": [
 *       [1, 3, 6],
 *       [2, 4, 5],
 *       ...
 *     ],
 *   }
 *   "children_settings" : { .. },
 *   "out_of_range": "ErrorRoute"
 * }
 *
 * "shards" can be an array of strings of comma-separated
 * shard ids, as stated in ShardSelectionRoute above
 *
 * NOTE:
 *  - "shards" and "pool" must have the same number of entries, in exactly
 *    the same order (e.g. `shards[5]` shows the shards processed by
 *    `pool.servers[5]`).
 *
 * @tparam ShardSelector Class responsible for selecting the shard responsible
 *                       for handling the request. The ShardSelector constructor
 *                       accepts a shardsMap (unordered_map) that maps
 *                       shardId -> destinationId.
 * @tparam MapType       C++ type container that maps shardId -> destinationId.
 *
 * @param factory               RouteHandleFactory to create destinations.
 * @param json                  JSON object with RouteHandle representation.
 */
template <
    class RouterInfo,
    class ShardSelector,
    class MapType = std::unordered_map<uint32_t, uint32_t>>
typename RouterInfo::RouteHandlePtr createEagerShardSelectionRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json);

} // namespace mcrouter
} // namespace memcache
} // namespace facebook

#include "ShardSelectionRouteFactory-inl.h"
