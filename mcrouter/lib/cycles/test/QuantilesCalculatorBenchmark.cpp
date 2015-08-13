/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <algorithm>
#include <cstdint>
#include <random>

#include <gflags/gflags.h>
#include <folly/Benchmark.h>
#include <folly/Random.h>

#include "mcrouter/lib/cycles/QuantilesCalculator.h"
#include "mcrouter/lib/cycles/test/QuantilesTestHelper.h"

using namespace facebook::memcache::cycles;
using namespace facebook::memcache::cycles::test;

BENCHMARK(Exact, n) {
  ExactCalculator ec;
  for (size_t i = 0; i < n; ++i) {
    ec.insert(folly::Random::rand32());
  }
  uint64_t res = ec.query(0.5);
  folly::doNotOptimizeAway(res);
}

BENCHMARK_RELATIVE(Boost, n) {
  BoostCalculator bc;
  for (size_t i = 0; i < n; ++i) {
    bc.insert(folly::Random::rand32());
  }
  uint64_t res = bc.query();
  folly::doNotOptimizeAway(res);
}

BENCHMARK_RELATIVE(Gk, n) {
  QuantilesCalculator<uint64_t> qc(0.01);
  for (size_t i = 0; i < n; ++i) {
    qc.insert(folly::Random::rand32());
  }
  uint64_t res = qc.query(0.5);
  folly::doNotOptimizeAway(res);
}

/**
 * -bm_min_iters=1000000
 *
 * ============================================================================
 * QuantilesCalculatorBenchmark.cpp                relative  time/iter  iters/s
 * ============================================================================
 * Exact                                                       87.04ns   11.49M
 * Boost                                            180.22%    48.30ns   20.71M
 * Gk                                               223.97%    38.86ns   25.73M
 * ============================================================================
 */
int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
