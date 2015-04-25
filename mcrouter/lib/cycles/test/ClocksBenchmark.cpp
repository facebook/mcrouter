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

#include <gflags/gflags.h>
#include <folly/Benchmark.h>

#include "mcrouter/lib/cycles/Clocks.h"

using facebook::memcache::cycles::CyclesClock;

CyclesClock cyclesClock;
std::chrono::steady_clock steadyClock;

BENCHMARK(CyclesClock, n) {
  while (n--) {
    auto t = cyclesClock.ticks();
    folly::doNotOptimizeAway(t);
  }
}

BENCHMARK_RELATIVE(steady_clock, n) {
  while (n--) {
    auto t = steadyClock.now();
    folly::doNotOptimizeAway(t);
  }
}

/**
 * --bm_min_iters=1000000
 *
 * ============================================================================
 * ClocksBenchmark.cpp                             relative  time/iter  iters/s
 * ============================================================================
 * CyclesClock                                                  9.02ns  110.88M
 * steady_clock                                      37.59%    23.99ns   41.68M
 * ============================================================================
 */

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
