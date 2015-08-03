/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <array>
#include <thread>

#include <gflags/gflags.h>
#include <folly/Benchmark.h>

#include "mcrouter/lib/cycles/Cycles.h"

using namespace facebook::memcache;

namespace {
cycles::CyclesClock cyclesClock;
}

// Benchmark if we were using simply two RDTSC calls.
BENCHMARK(CyclesClock, n) {
  cycles::startExtracting([](cycles::CycleStats){});
  while (n--) {
    uint64_t startedAt = cyclesClock.read().ticks;
    uint64_t length = cyclesClock.read().ticks - startedAt;

    folly::doNotOptimizeAway(&length);
  }
  cycles::stopExtracting();
}

// Benchmark for invalid (not labeled) intervals.
BENCHMARK_RELATIVE(IntervalGuard_invalid, n) {
  cycles::startExtracting([](cycles::CycleStats){});
  while (n--) {
    cycles::IntervalGuard ig;
    folly::doNotOptimizeAway(&ig);
  }
  cycles::stopExtracting();
}

// Benchmark for when the API is disabled.
BENCHMARK_RELATIVE(IntervalGuard_disabled, n) {
  cycles::stopExtracting();
  while (n--) {
    cycles::IntervalGuard ig;
    cycles::label(1, 2);
    folly::doNotOptimizeAway(&ig);
  }
}

// Benchmark for valid.
BENCHMARK_RELATIVE(IntervalGuard_valid, n) {
  cycles::startExtracting([](cycles::CycleStats){});
  while (n--) {
    cycles::IntervalGuard ig;
    cycles::label(1, 2);
    folly::doNotOptimizeAway(&ig);
  }
  cycles::stopExtracting();
}


BENCHMARK_DRAW_LINE();


BENCHMARK_RELATIVE(IntervalGuard_MultiThread, n) {
  cycles::startExtracting([](cycles::CycleStats){});
  const size_t constexpr nThreads = 32;
  std::array<std::thread, nThreads> threads;
  for (size_t i = 0; i < nThreads; ++i) {
    threads[i] = std::thread([n]() mutable {
      while (n--) {
        cycles::IntervalGuard ig;
        cycles::label(1, 2);
        folly::doNotOptimizeAway(&ig);
      }
    });
  }
  for (size_t i = 0; i < nThreads; ++i) {
    threads[i].join();
  }
  cycles::stopExtracting();
}

/**
 * --bm_min_iters=1000000
 *
 * ============================================================================
 * CyclesBenchmark.cpp                             relative  time/iter  iters/s
 * ============================================================================
 * CyclesClock                                                 18.55ns   53.90M
 * IntervalGuard_invalid                            161.85%    11.46ns   87.23M
 * IntervalGuard_disabled                           222.24%     8.35ns  119.78M
 * IntervalGuard_valid                               59.46%    31.21ns   32.04M
 * ----------------------------------------------------------------------------
 * IntervalGuard_MultiThread                         28.89%    64.23ns   15.57M
 * ============================================================================
 */
int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
