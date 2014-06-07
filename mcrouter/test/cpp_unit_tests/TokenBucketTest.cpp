#include <gtest/gtest.h>

#include "mcrouter/TokenBucket.h"

namespace {

using facebook::memcache::mcrouter::TokenBucket;

void doTokenBucketTest(double maxQps, double consumeSize) {
  const double tenMillisecondBurst = maxQps * 0.010;
  // Select a burst size of 10 milliseconds at the max rate or the consume size
  // if 10 ms at maxQps is too small.
  const double burstSize = std::max(consumeSize, tenMillisecondBurst);
  TokenBucket tokenBucket(maxQps, burstSize, 0);
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
    EXPECT_GE(maxQps * currentTime, tokenCounter);
  }
}

TEST(TokenBucket, sanity) {
  doTokenBucketTest(100, 1);
  doTokenBucketTest(1000, 1);
  doTokenBucketTest(10000, 1);
  // Consume more than one at a time.
  doTokenBucketTest(10000, 5);
}

TEST(TokenBucket, ReverseTime) {
  const double rate = 1000;
  TokenBucket tokenBucket(rate, rate * 0.01, 0);
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

} // anonymous namespace
