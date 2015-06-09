/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ShardHashFunc.h"

#include <folly/dynamic.h>

namespace facebook { namespace memcache { namespace mcrouter {

bool getShardId(folly::StringPiece key, folly::StringPiece& shardId) {
  size_t colon = qfind(key, ':');
  if (colon == std::string::npos) {
    return false;
  }
  key.advance(colon + 1);
  colon = qfind(key, ':');
  if (colon == std::string::npos) {
    return false;
  }
  if (colon <= 0 || !isdigit(key[colon - 1])) {
    return false;
  }
  shardId = key.subpiece(0, colon);
  return true;
}

ConstShardHashFunc::ConstShardHashFunc(size_t n)
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
