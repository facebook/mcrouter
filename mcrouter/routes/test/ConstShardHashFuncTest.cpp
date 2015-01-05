/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "mcrouter/routes/ShardHashFunc.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

TEST(constShardHashFuncTest, picksRightShard) {
  ConstShardHashFunc func(4);

  EXPECT_EQ(3, func("b:3:meh"));
  EXPECT_EQ(2, func("bl:2:meh"));
  EXPECT_EQ(1, func("bla:1:meh"));
  EXPECT_EQ(0, func("blah:0:meh"));
}

TEST(constShardHashFuncTest, ch3Fallback) {
  ConstShardHashFunc func(4);

  // not valid keys, routed with Ch3 fallback
  EXPECT_EQ(2, func("blahmeh"));
  EXPECT_EQ(0, func("blah:meh"));
  EXPECT_EQ(1, func("blah::meh"));
  EXPECT_EQ(0, func("blah:12c34:meh"));
  EXPECT_EQ(3, func("blah:4:meh"));
}
