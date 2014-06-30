/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <gtest/gtest.h>

#include <string>

#include "folly/Benchmark.h"
#include "mcrouter/config.h"
#include "mcrouter/routes/ShardHashFunc.h"

using namespace facebook::memcache::mcrouter;
using namespace facebook::memcache;

using folly::StringPiece;

void fillSampleData(StringKeyedUnorderedMap<size_t>* shardMap) {
  shardMap->insert({"8001", 3});
  shardMap->insert({"8002", 2});
  shardMap->insert({"8003", 1});
  shardMap->insert({"8004", 0});
  shardMap->insert({"8005", -1});
  shardMap->insert({"8006", 1000});
}

TEST(shardHashFuncTest, picksRightShard) {
  StringKeyedUnorderedMap<size_t> shardMap;
  fillSampleData(&shardMap);
  ShardHashFunc func(shardMap, 4);

  // values from shardMap
  EXPECT_EQ(3, func("b:8001:meh"));
  EXPECT_EQ(2, func("bl:8002:meh"));
  EXPECT_EQ(1, func("bla:8003:meh"));
  EXPECT_EQ(0, func("blah:8004:meh"));
}

TEST(shardHashFuncTest, ch3Fallback) {
  StringKeyedUnorderedMap<size_t> shardMap;
  fillSampleData(&shardMap);
  ShardHashFunc func(shardMap, 4);

  // not found or valid in shardMap, routed with Ch3 fallback
  EXPECT_EQ(3, func("blah:8005:meh"));
  EXPECT_EQ(3, func("blah:8006:meh"));
  EXPECT_EQ(1, func("blah:8007:meh"));
}

BENCHMARK(unorderedMapBenchmark, iters) {
  const int buckets = 9001;
  const int maxKeyLength = 20;
  char keys[buckets][maxKeyLength];
  char buf[maxKeyLength - sizeof("blah::meh") + 1];
  std::unique_ptr<ShardHashFunc> func;

  BENCHMARK_SUSPEND {
    StringKeyedUnorderedMap<size_t> shardMap;
    std::string key_ns;
    for (size_t i = 0; i < buckets; i++) {
      snprintf(buf, sizeof(buf), "%ld", i * 23917 % buckets);
      snprintf(keys[i], maxKeyLength, "blah:%ld:meh", i);
      key_ns = std::string(buf);
      shardMap.insert({key_ns, i});
    }
    func = std::unique_ptr<ShardHashFunc>(new ShardHashFunc(shardMap, buckets));
  }

  for (size_t i = 0; i < iters; i++) {
    EXPECT_EQ(i * 239 % buckets,
              (*func)(StringPiece(keys[i * 239 * 23917 % buckets])));
  }
}
