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

#include "mcrouter/TokenBucket.h"
#include "mcrouter/AtomicTokenBucket.h"

namespace {

using facebook::memcache::mcrouter::AtomicTokenBucket;
using facebook::memcache::mcrouter::DynamicAtomicTokenBucket;
using facebook::memcache::mcrouter::TokenBucket;

template <typename TokenBucketType>
void doTokenBucketTest(double maxQps, double consumeSize) {
  const double tenMillisecondBurst = maxQps * 0.010;
  // Select a burst size of 10 milliseconds at the max rate or the consume size
  // if 10 ms at maxQps is too small.
  const double burstSize = std::max(consumeSize, tenMillisecondBurst);
  TokenBucketType tokenBucket(maxQps, burstSize, 0);
  double tokenCounter = 0;
  double currentTime = 0;
  // Simulate time advancing 10 seconds
  for (; currentTime <= 10.0; currentTime += 0.001) {
    EXPECT_FALSE(tokenBucket.consume(burstSize + 1, currentTime));
    while (tokenBucket.consume(consumeSize, currentTime)) {
      tokenCounter += consumeSize;
    }
    // Tokens consumed should exceed some lower bound based on maxQps.
    // Note: The token bucket implementation is not precise, so the lower bound
    // is somewhat fudged. The upper bound is accurate however.
    EXPECT_LE(maxQps * currentTime * 0.9 - 1, tokenCounter);
    // Tokens consumed should not exceed some upper bound based on maxQps.
    EXPECT_GE(maxQps * currentTime + 1e-6, tokenCounter);
  }
}

template <typename TokenBucketType>
void doReverseTimeTest() {
  const double rate = 1000;
  TokenBucketType tokenBucket(rate, rate * 0.01 + 1e-6, 0);
  size_t count = 0;
  while (tokenBucket.consume(1, 0.1)) {
    count += 1;
  }
  EXPECT_EQ(10, count);
  // Going backwards in time has no affect on the toke count (this protects
  // against different threads providing out of order timestamps).
  double tokensBefore = tokenBucket.available();
  EXPECT_FALSE(tokenBucket.consume(1, 0.09999999));
  EXPECT_EQ(tokensBefore, tokenBucket.available());
}

TEST(TokenBucket, sanity) {
  doTokenBucketTest<TokenBucket>(100, 1);
  doTokenBucketTest<TokenBucket>(1000, 1);
  doTokenBucketTest<TokenBucket>(10000, 1);
  // Consume more than one at a time.
  doTokenBucketTest<TokenBucket>(10000, 5);
}

TEST(TokenBucket, ReverseTime) {
  doReverseTimeTest<TokenBucket>();
}

TEST(AtomicTokenBucket, sanity) {
  doTokenBucketTest<AtomicTokenBucket>(100, 1);
  doTokenBucketTest<AtomicTokenBucket>(1000, 1);
  doTokenBucketTest<AtomicTokenBucket>(10000, 1);
  // Consume more than one at a time.
  doTokenBucketTest<AtomicTokenBucket>(10000, 5);
}

TEST(AtomicTokenBucket, ReverseTime) {
  doReverseTimeTest<AtomicTokenBucket>();
}

TEST(AtomicTokenBucket, drainOnFail) {
  DynamicAtomicTokenBucket tokenBucket;

  // Almost empty the bucket
  EXPECT_TRUE(tokenBucket.consume(9, 10, 10, 1));

  // Request more tokens than available
  EXPECT_FALSE(tokenBucket.consume(5, 10, 10, 1));
  EXPECT_DOUBLE_EQ(1.0, tokenBucket.available(10, 10, 1));

  // Again request more tokens than available, but ask to drain
  EXPECT_DOUBLE_EQ(1.0, tokenBucket.consumeOrDrain(5, 10, 10, 1));
  EXPECT_DOUBLE_EQ(0.0, tokenBucket.consumeOrDrain(1, 10, 10, 1));
}

} // anonymous namespace
