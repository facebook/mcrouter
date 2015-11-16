/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <chrono>
#include <thread>
#include <vector>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/Optional.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "mcrouter/LeaseTokenMap.h"

using namespace facebook::memcache::mcrouter;

namespace {

void assertQueryTrue(LeaseTokenMap& map, uint64_t specialToken,
                     LeaseTokenMap::Item expectedItem) {
  auto item = map.query(specialToken);
  EXPECT_TRUE(item.hasValue());
  EXPECT_EQ(item->originalToken, expectedItem.originalToken);
  EXPECT_EQ(item->poolName, expectedItem.poolName);
  EXPECT_EQ(item->indexInPool, expectedItem.indexInPool);
}

void assertQueryFalse(LeaseTokenMap& map, uint64_t specialToken) {
  auto item = map.query(specialToken);
  EXPECT_FALSE(item.hasValue());
}

} // anonymous namespace

TEST(LeaseTokenMap, sanity) {
  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread);

  EXPECT_EQ(map.size(), 0);

  auto tkn1 = map.insert({10, "pool", 1});
  auto tkn2 = map.insert({20, "pool", 2});
  auto tkn3 = map.insert({30, "pool", 3});

  EXPECT_EQ(map.size(), 3);

  assertQueryTrue(map, tkn1, {10, "pool", 1});
  assertQueryTrue(map, tkn2, {20, "pool", 2});
  assertQueryTrue(map, tkn3, {30, "pool", 3});

  EXPECT_EQ(map.size(), 0); // read all data from map.
  assertQueryFalse(map, 1); // "existing" id but without magic.
  assertQueryFalse(map, 10); // unexisting id.
  assertQueryFalse(map, 0x7aceb00c00000006); // unexisintg special token.
}

TEST(LeaseTokenMap, magicConflict) {
  // If we are unlucky enough to have an originalToken (i.e. token returned
  // by memcached) that contains our "magic", LeaseTokenMap should handle it
  // gracefully.

  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread);

  EXPECT_EQ(map.size(), 0);

  uint64_t originalToken = 0x7aceb00c0000000A;
  uint64_t specialToken = map.insert({originalToken, "pool", 1});

  EXPECT_EQ(map.size(), 1);
  assertQueryTrue(map, specialToken, {originalToken, "pool", 1});
  assertQueryFalse(map, originalToken);
  EXPECT_EQ(map.size(), 0);
}

TEST(LeaseTokenMap, shrink) {
  size_t tokenTtl = 100;
  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread, tokenTtl);

  EXPECT_EQ(map.size(), 0);

  for (size_t i = 0; i < 1000; ++i) {
    map.insert({i * 10, "pool", i});
  }

  // Allow time for the map to shrink.
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(tokenTtl * 5));

  EXPECT_EQ(map.size(), 0);
}

TEST(LeaseTokenMap, stress) {
  size_t tokenTtl = 1000;
  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread, tokenTtl);

  EXPECT_EQ(map.size(), 0);

  for (size_t i = 0; i < 5000; ++i) {
    uint64_t origToken = i * 10;
    uint64_t specToken = map.insert({origToken, "pool", i});

    /* sleep override */
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // leave some work for the shrink thread.
    if (i % 10 != 0) {
      assertQueryTrue(map, specToken, {origToken, "pool", i});
    }
  }
}
