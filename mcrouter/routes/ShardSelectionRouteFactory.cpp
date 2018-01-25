/*
 *  Copyright (c) 2017-present, Facebook, Inc.
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

} // namespace

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

void parseShardsPerServerJson(
    const folly::dynamic& jShards,
    std::function<void(uint32_t)>&& handleShardFunc) {
  std::vector<size_t> shards;
  if (jShards.isArray()) {
    shards = parseShardsJsonArray(jShards);
  } else if (jShards.isString()) {
    shards = parseShardsJsonString(jShards);
  } else {
    throwLogic(
        "EagerShardSelectionRoute: 'shards[{}]' must be an array of "
        "integers or a string of comma-separated shard ids.",
        shards);
  }
  for (auto shard : shards) {
    handleShardFunc(shard);
  }
}

} // namespace detail

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
