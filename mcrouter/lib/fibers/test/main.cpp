#include <gtest/gtest.h>

#include "folly/Benchmark.h"

// for backward compatibility with gflags
namespace gflags { }
namespace google { using namespace gflags; }

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto rc = RUN_ALL_TESTS();
  folly::runBenchmarksOnFlag();
  return rc;
}
