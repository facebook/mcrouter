/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/experimental/StringKeyedUnorderedMap.h>
#include <folly/Range.h>

#include "mcrouter/config.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

class ShardSplitter {
 public:
  explicit ShardSplitter(const folly::dynamic& json);

  /**
   * Returns number of shard splits for this key. If this number
   * is greater than one, stores shardId part of the key in shardId.
   *
   * @return 1 if key shouldn't be split; number of splits (> 1) otherwise.
   */
  size_t getShardSplitCnt(folly::StringPiece key,
                          folly::StringPiece& shardId) const;

  const folly::StringKeyedUnorderedMap<size_t>& getShardSplits() const {
    return shardSplits_;
  }
 private:
  folly::StringKeyedUnorderedMap<size_t> shardSplits_;
};

}}}  // facebook::memcache::mcrouter
