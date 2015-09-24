/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ShardSplitRoute.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeShardSplitRoute(
  McrouterRouteHandlePtr rh,
  ShardSplitter shardSplitter) {

  return std::make_shared<McrouterRouteHandle<ShardSplitRoute>>(
    std::move(rh), std::move(shardSplitter));
}

std::string shardSplitSuffix(size_t offset) {
  std::string ret;
  ret.push_back('a' + (offset % 26));
  ret.push_back('a' + (offset / 26));
  return ret;
}

std::string createSplitKey(folly::StringPiece fullKey,
                           size_t offset,
                           folly::StringPiece shard) {
  std::string newKey;
  newKey.reserve(fullKey.size() + 2);
  newKey.append(fullKey.begin(), shard.end());
  newKey.push_back('a' + (offset % 26));
  newKey.push_back('a' + (offset / 26));
  newKey.append(shard.end(), fullKey.end());
  return newKey;
}

}}}  // facebook::memcache::mcrouter
