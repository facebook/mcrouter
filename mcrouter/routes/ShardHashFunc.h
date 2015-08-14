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

#include <string>

#include <folly/Range.h>

#include "mcrouter/lib/Ch3HashFunc.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Returns shard id part of a key. Shard id is first number surrounded by
 * colons in the key: prefix:[0-9]+:suffix
 *                           ^^^^^^
 *                           shardId
 *
 * @param [in] key Any string
 * @param [out] shardId Subpiece of original key
 *
 * @return if key has shard id, stores subpiece in shardId and returns true;
 *         otherwise returns false and doesn't change shardId.
 */
bool getShardId(folly::StringPiece key, folly::StringPiece& shardId);

/**
 * Shard hash function for const sharding. This function
 * assumes that the lookup key in the given key is the actual
 * shard number to return.
 * For eg: For key "key:1234:blah" the result is 1234.
 */
class ConstShardHashFunc {
 public:
  explicit ConstShardHashFunc(size_t n);

  size_t operator()(folly::StringPiece key) const;

  static const char* type() { return "ConstShard"; }

 private:
  size_t n_;
  Ch3HashFunc ch3_;

  bool shardLookup(folly::StringPiece key, size_t* result) const;
};

}}}  // facebook::memcache::mcrouter
