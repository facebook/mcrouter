/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <string>

#include "folly/Range.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/Ch3HashFunc.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

class ShardHashFunc {
 public:
  ShardHashFunc(StringKeyedUnorderedMap<size_t> shardMap, size_t n);

  ShardHashFunc(const folly::dynamic& json, size_t n);

  size_t operator()(folly::StringPiece key) const;

  static std::string type() { return "Shard"; }

 private:
  StringKeyedUnorderedMap<size_t> shardMap_;
  size_t n_;
  Ch3HashFunc ch3_;

  bool shardLookup(folly::StringPiece key, size_t* result) const;
};

/**
 * Shard hash function for const sharding. This function
 * assumes that the lookup key in the given key is the actual
 * shard number to return.
 * For eg: For key "key:1234:blah" the result is 1234.
 */
class ConstShardHashFunc {
 public:
  explicit ConstShardHashFunc(size_t n);

  ConstShardHashFunc(const folly::dynamic& json, size_t n);

  size_t operator()(folly::StringPiece key) const;

  static std::string type() { return "ConstShard"; }

 private:
  size_t n_;
  Ch3HashFunc ch3_;

  bool shardLookup(folly::StringPiece key, size_t* result) const;
};

}}}  // facebook::memcache::mcrouter
