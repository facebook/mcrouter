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
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "mcrouter/LeaseTokenMap.h"

using facebook::memcache::AccessPoint;
using namespace facebook::memcache::mcrouter;

namespace {

void assertQueryTrue(LeaseTokenMap& map, uint64_t specialToken,
                     uint64_t expectedOriginalToken,
                     std::chrono::milliseconds expectedTimeout) {
  uint64_t originalToken;
  std::shared_ptr<const AccessPoint> ap;
  std::chrono::milliseconds timeout;
  EXPECT_TRUE(map.query(specialToken, originalToken, ap, timeout));
  EXPECT_EQ(originalToken, expectedOriginalToken);
  EXPECT_EQ(timeout, expectedTimeout);
}

void assertQueryFalse(LeaseTokenMap& map, uint64_t specialToken) {
  uint64_t originalToken;
  std::shared_ptr<const AccessPoint> ap;
  std::chrono::milliseconds timeout;
  EXPECT_FALSE(map.query(specialToken, originalToken, ap, timeout));
}

} // anonymous namespace

TEST(LeaseTokenMap, sanity) {
  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread);

  EXPECT_EQ(map.size(), 0);

  auto tkn1 = map.insert(10, nullptr, std::chrono::milliseconds(30));
  auto tkn2 = map.insert(20, nullptr, std::chrono::milliseconds(20));
  auto tkn3 = map.insert(30, nullptr, std::chrono::milliseconds(10));

  EXPECT_EQ(map.size(), 3);

  assertQueryTrue(map, tkn1, 10, std::chrono::milliseconds(30));
  assertQueryTrue(map, tkn2, 20, std::chrono::milliseconds(20));
  assertQueryTrue(map, tkn3, 30, std::chrono::milliseconds(10));

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
  uint64_t specialToken = map.insert(originalToken, nullptr,
                                     std::chrono::milliseconds(10));

  EXPECT_EQ(map.size(), 1);
  assertQueryTrue(map, specialToken, originalToken,
                  std::chrono::milliseconds(10));
  assertQueryFalse(map, originalToken);
  EXPECT_EQ(map.size(), 0);
}

TEST(LeaseTokenMap, shrink) {
  size_t tokenTtl = 100;
  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread, tokenTtl);

  EXPECT_EQ(map.size(), 0);

  for (int i = 0; i < 1000; ++i) {
    map.insert(i * 10, nullptr, std::chrono::milliseconds(i));
  }

  // Allow time for the map to shrink.
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(tokenTtl * 5));

  EXPECT_EQ(map.size(), 0);
}

TEST(LeaseTokenMap, lowTtl) {
  size_t tokenTtl = 1;
  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread, tokenTtl);

  EXPECT_EQ(map.size(), 0);

  for (int i = 0; i < 10000; ++i) {
    uint64_t origToken = i * 10;
    uint64_t specToken = map.insert(origToken, nullptr,
                                    std::chrono::milliseconds(i));

    // leave some work for the shrink thread.
    if (i % 10 != 0) {
      assertQueryTrue(map, specToken, origToken, std::chrono::milliseconds(i));
    }
  }

  // Allow time for the map to shrink.
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(map.size(), 0);
}

TEST(LeaseTokenMap, stress) {
  size_t tokenTtl = 1000;
  folly::ScopedEventBaseThread evbAuxThread;
  LeaseTokenMap map(evbAuxThread, tokenTtl);

  EXPECT_EQ(map.size(), 0);

  for (int i = 0; i < 5000; ++i) {
    uint64_t origToken = i * 10;
    uint64_t specToken = map.insert(origToken, nullptr,
                                    std::chrono::milliseconds(i));

    /* sleep override */
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // leave some work for the shrink thread.
    if (i % 10 != 0) {
      assertQueryTrue(map, specToken, origToken, std::chrono::milliseconds(i));
    }
  }
}
