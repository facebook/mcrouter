/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ShardHashFunc.h"

#include <folly/dynamic.h>

#include "mcrouter/lib/fbi/cpp/util.h"

using std::string;

namespace facebook { namespace memcache { namespace mcrouter {

bool getShardId(folly::StringPiece key, folly::StringPiece& shardId) {
  size_t colon = qfind(key, ':');
  if (colon == string::npos) {
    return false;
  }
  key.advance(colon + 1);
  colon = qfind(key, ':');
  if (colon == string::npos) {
    return false;
  }
  if (colon <= 0 || !isdigit(key[colon - 1])) {
    return false;
  }
  shardId = key.subpiece(0, colon);
  return true;
}

ShardHashFunc::ShardHashFunc(StringKeyedUnorderedMap<size_t> shardMap, size_t n)
    : shardMap_(std::move(shardMap)),
      n_(n),
      ch3_(n) {
}

ShardHashFunc::ShardHashFunc(const folly::dynamic& json, size_t n)
      : n_(n),
        ch3_(n) {
  checkLogic(json.isObject() && json.count("shard_map"),
             "ShardHashFunc should be object with shard_map");

  const auto& shardMap = json["shard_map"];
  checkLogic(shardMap.isObject(), "ShardHashFunc: shard_map is not object");

  for (const auto& it : shardMap.items()) {
    checkLogic(it.first.isString(),
               "ShardHashFunc: shard_map key is not string");
    checkLogic(it.second.isInt(),
               "ShardHashFunc: shard_map value is not int");

    shardMap_.emplace(it.first.asString().toStdString(), it.second.asInt());
  }
}

size_t ShardHashFunc::operator()(folly::StringPiece key) const {
  size_t index;
  if (shardLookup(key, &index)) {
    return index;
  }
  return ch3_(key);
}

bool ShardHashFunc::shardLookup(folly::StringPiece key, size_t* result) const {
  folly::StringPiece shard;
  if (!getShardId(key, shard)) {
    return false;
  }
  auto it = shardMap_.find(shard);
  if (it == shardMap_.end()) {
    return false; // not found
  }
  auto index = it->second;
  if (index >= n_) {
    return false; // out of bounds
  }
  *result = index;
  return true;
}

ConstShardHashFunc::ConstShardHashFunc(size_t n)
    : n_(n),
      ch3_(n) {
}

ConstShardHashFunc::ConstShardHashFunc(const folly::dynamic& json, size_t n)
    : n_(n),
      ch3_(n) {
}

size_t ConstShardHashFunc::operator()(folly::StringPiece key) const {
  size_t index;
  if (shardLookup(key, &index)) {
    return index;
  }
  return ch3_(key);
}

bool ConstShardHashFunc::shardLookup(folly::StringPiece key,
                                     size_t* result) const {
  folly::StringPiece shard;
  if (!getShardId(key, shard)) {
    return false;
  }
  for (const auto& iter: shard) {
    if (!isdigit(iter)) {
      return false;
    }
  }
  size_t index;
  try {
    index = folly::to<size_t>(shard);
  } catch (...) {
    return false;
  }

  if (index >= n_) {
    return false; // out of bounds
  }
  *result = index;
  return true;
}

}}}  // facebook::memcache::mcrouter
