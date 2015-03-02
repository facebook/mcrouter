/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ShardSplitter.h"

#include <folly/dynamic.h>

#include "mcrouter/routes/ShardHashFunc.h"

namespace facebook { namespace memcache { namespace mcrouter {

constexpr size_t kMaxSplits = 26 * 26;

ShardSplitter::ShardSplitter(const folly::dynamic& json) {
  if (!json.isObject()) {
    return;
  }

  for (const auto& it : json.items()) {
    if (!it.second.isInt()) {
      LOG(ERROR) << "ShardSplitter: shard_splits value is not an int for "
                 << it.first.asString();
      continue;
    }

    auto splitCnt = it.second.asInt();
    if (splitCnt <= 0) {
      LOG(ERROR) << "ShardSplitter: shard_splits value <= 0 '"
                 << it.first.asString() << "': " << splitCnt;
    } else if (static_cast<size_t>(splitCnt) > kMaxSplits) {
      LOG(ERROR) << "ShardSplitter: shard_splits value > " << kMaxSplits
                 << " '" << it.first.asString() << "': " << splitCnt;
      shardSplits_.emplace(it.first.c_str(), kMaxSplits);
    } else {
      shardSplits_.emplace(it.first.c_str(), splitCnt);
    }
  }
}

size_t ShardSplitter::getShardSplitCnt(folly::StringPiece routingKey,
                                       folly::StringPiece& shard) const {
  if (!getShardId(routingKey, shard)) {
    return 1;
  }

  auto splitIt = shardSplits_.find(shard);
  if (splitIt == shardSplits_.end()) {
    return 1;
  }
  return splitIt->second;
}


}}}  // facebook::memcache::mcrouter
