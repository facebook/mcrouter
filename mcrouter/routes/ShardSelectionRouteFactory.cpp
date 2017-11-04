/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ShardSelectionRouteFactory.h"

#include <algorithm>
#include <cassert>

#include <folly/Random.h>
#include <folly/dynamic.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

namespace {

std::vector<size_t> parseShardsJsonArray(const folly::dynamic& shardsJson) {
  assert(shardsJson.isArray());

  std::vector<size_t> shards;
  shards.reserve(shardsJson.size());

  for (size_t j = 0; j < shardsJson.size(); ++j) {
    const auto& shardIdJson = shardsJson[j];
    checkLogic(
        shardIdJson.isInt(),
        "ShardSelectionRoute: 'shards' property expected to be an "
        "array of integers. Invalid shard found in array: {}",
        shardIdJson);
    shards.push_back(static_cast<size_t>(shardIdJson.asInt()));
  }

  return shards;
}

std::vector<size_t> parseShardsJsonString(const folly::dynamic& shardsJson) {
  assert(shardsJson.isString());

  std::vector<size_t> shards;

  auto shardsStr = shardsJson.stringPiece();
  while (!shardsStr.empty()) {
    auto shardId = shardsStr.split_step(',');
    try {
      shards.push_back(folly::to<size_t>(shardId));
    } catch (const std::exception& e) {
      throwLogic(
          "ShardSelectionRoute: 'shards' property expected to be a string of "
          "comma-separated integers. Invalid shard found in string: {}. "
          "Exception: {}",
          shardId,
          e.what());
    }
  }

  return shards;
}

std::vector<std::vector<size_t>> parseAllShardsJson(
    const folly::dynamic& allShardsJson) {
  assert(allShardsJson.isArray());

  std::vector<std::vector<size_t>> allShards;
  allShards.reserve(allShardsJson.size());

  for (size_t i = 0; i < allShardsJson.size(); ++i) {
    const auto& shardsJson = allShardsJson[i];
    if (shardsJson.isArray()) {
      allShards.push_back(parseShardsJsonArray(shardsJson));
    } else if (shardsJson.isString()) {
      allShards.push_back(parseShardsJsonString(shardsJson));
    } else {
      throwLogic(
          "ShardSelectionRoute: 'shards[{}]' must be an array of integers or a "
          "string of comma-separated shard ids.",
          i);
    }
  }

  return allShards;
}

size_t getMaxShardId(const std::vector<std::vector<size_t>>& allShards) {
  size_t maxShardId = 0;
  for (const auto& shards : allShards) {
    for (auto shardId : shards) {
      maxShardId = std::max(maxShardId, shardId);
    }
  }
  return maxShardId;
}

} // namespace

const folly::dynamic& getPoolJson(const folly::dynamic& json) {
  assert(json.isObject());

  auto poolJson = json.get_ptr("pool");
  checkLogic(poolJson, "ShardSelectionRoute: 'pool' not found");
  return *poolJson;
}

const folly::dynamic& getShardsJson(const folly::dynamic& json) {
  assert(json.isObject());

  auto shardsJson = json.get_ptr("shards");
  checkLogic(
      shardsJson && shardsJson->isArray(),
      "ShardSelectionRoute: 'shards' not found or not an array");
  return *shardsJson;
}

template <>
std::vector<uint16_t> getShardsMap(
    const folly::dynamic& json,
    size_t numDestinations) {
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
  constexpr uint16_t kNoDestination = std::numeric_limits<uint16_t>::max();
  std::vector<uint16_t> shardsMap(shardsMapSize, kNoDestination);

  // We don't need to validate here, as it was validated before.
  for (size_t i = 0; i < allShards.size(); ++i) {
    for (size_t j = 0; j < allShards[i].size(); ++j) {
      size_t shard = allShards[i][j];
      if (shardsMap[shard] == kNoDestination || shardsMap[shard] == i) {
        shardsMap[shard] = i;
      } else {
        LOG(WARNING) << "ShardSelectionRoute: shard " << shard
                     << " is served by two destinations (" << shardsMap[shard]
                     << " and " << i << "). Picking one destination randomly.";
        if (folly::Random::oneIn(2)) {
          shardsMap[shard] = i;
        }
      }
    }
  }

  return shardsMap;
}

template <>
std::unordered_map<uint32_t, uint16_t> getShardsMap(
    const folly::dynamic& json,
    size_t numDestinations) {
  assert(json.isArray());

  checkLogic(
      numDestinations < std::numeric_limits<uint16_t>::max(),
      "ShardSelectionRoute: Only up to {} destinations are supported. "
      "Current number of destinations: {}",
      std::numeric_limits<uint16_t>::max() - 1,
      numDestinations);

  // Validate and get a list of shards.
  auto allShards = parseAllShardsJson(json);

  std::unordered_map<uint32_t, uint16_t> shardsMap;

  // We don't need to validate here, as it was validated before.
  for (size_t i = 0; i < allShards.size(); ++i) {
    for (size_t j = 0; j < allShards[i].size(); ++j) {
      size_t shard = allShards[i][j];
      if (shardsMap.find(shard) == shardsMap.end()) {
        shardsMap[shard] = i;
      } else {
        LOG(WARNING) << "ShardSelectionRoute: shard " << shard
                     << " is served by two destinations (" << shardsMap[shard]
                     << " and " << i << "). Picking one destination randomly.";
        if (folly::Random::oneIn(2)) {
          shardsMap[shard] = i;
        }
      }
    }
  }

  return shardsMap;
}

} // namespace detail

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
