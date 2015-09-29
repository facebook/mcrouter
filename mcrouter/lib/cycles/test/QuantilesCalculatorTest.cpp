/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>
#include <folly/Random.h>

#include "mcrouter/lib/cycles/QuantilesCalculator.h"
#include "mcrouter/lib/cycles/test/QuantilesTestHelper.h"

using namespace facebook::memcache::cycles;
using namespace facebook::memcache::cycles::test;

TEST(QuantilesCalculator, empty) {
  QuantilesCalculator<uint64_t> qc(0.001);
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(), qc.query(0.5));
}

TEST(QuantilesCalculator, basic) {
  QuantilesCalculator<uint64_t> qc(0.001);
  for (int i = 1; i <= 10; ++i) {
    qc.insert(i);
  }
  EXPECT_EQ(5, qc.query(0.5));
  EXPECT_EQ(10, qc.size());
}

TEST(QuantilesCalculator, basicRandom) {
  QuantilesCalculator<uint64_t> qc(0.001);
  ExactCalculator e;
  for (int i = 0; i < 30; ++i) {
    uint64_t num = folly::Random::rand64();
    qc.insert(num);
    e.insert(num);
    EXPECT_EQ(e.query(0.5), qc.query(0.5));
  }
}

TEST(QuantilesCalculator, repeatedPrecision) {
  const double eps = 0.01;
  QuantilesCalculator<uint64_t> qc(eps);
  ExactCalculator e;
  for (int j = 0; j < 10; ++j) {
    for (int i = 1; i <= 10000; ++i) {
      qc.insert(i);
      e.insert(i);
    }
  }

  EXPECT_GE(qc.query(0.5), e.query(0.5 - eps));
  EXPECT_LE(qc.query(0.5), e.query(0.5 + eps));
}

TEST(QuantilesCalculator, randomPrecision) {
  const double eps = 0.001;
  QuantilesCalculator<uint64_t> qc(eps);
  ExactCalculator e;
  for (int i = 0; i < 1000000; ++i) {
    uint64_t num = folly::Random::rand32();
    qc.insert(num);
    e.insert(num);
  }

  EXPECT_GE(qc.query(0.5), e.query(0.5 - eps));
  EXPECT_LE(qc.query(0.5), e.query(0.5 + eps));
}

TEST(QuantilesCalculator, normalDistributionPrecision) {
  const double eps = 0.005;
  QuantilesCalculator<uint64_t> qc(eps);
  ExactCalculator e;
  for (int i = 0; i < 1000000; ++i) {
    uint64_t num = normalRnd();
    qc.insert(num);
    e.insert(num);
  }

  EXPECT_GE(qc.query(0.5), e.query(0.5 - eps));
  EXPECT_LE(qc.query(0.5), e.query(0.5 + eps));
}

TEST(QuantilesCalculator, size) {
  const double eps = 0.001;
  const size_t iters = 100000;
  QuantilesCalculator<uint64_t> qc(eps);
  for (size_t i = 1; i <= iters; ++i) {
    qc.insert(normalRnd());
    uint64_t maxSize = (1 / eps) * std::log2(iters * eps);
    EXPECT_LE(qc.internalSize(), maxSize);
  }
  EXPECT_EQ(iters, qc.size());
}
