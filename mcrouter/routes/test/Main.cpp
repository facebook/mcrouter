/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include <folly/Benchmark.h>

// for backward compatibility with gflags
namespace gflags {} // gflags
namespace google {
using namespace gflags;
} // google

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  gflags::SetCommandLineOptionWithMode(
      "bm_min_iters", "100000", gflags::SET_FLAG_IF_DEFAULT);
  folly::runBenchmarksOnFlag();
  return result;
}
