#include <gtest/gtest.h>

#include "folly/Benchmark.h"

// for backward compatibility with gflags
namespace gflags { }
namespace google { using namespace gflags; }

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::SetCommandLineOptionWithMode(
    "bm_min_iters", "100000", google::SET_FLAG_IF_DEFAULT
  );
  folly::runBenchmarksOnFlag();
  return result;
}
