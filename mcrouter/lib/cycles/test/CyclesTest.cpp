/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <thread>

#include <gtest/gtest.h>
#include <folly/Memory.h>

#include "mcrouter/lib/cycles/Clocks.h"
#include "mcrouter/lib/cycles/Cycles.h"

using namespace facebook::memcache;

class CyclesTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    cycles::startExtracting([](cycles::CycleStats){});
  }

  virtual void TearDown() {
    cycles::stopExtracting();
  }
};

TEST_F(CyclesTest, basic) {
  cycles::IntervalGuard ig;
  EXPECT_TRUE(cycles::label(1, 2));
}

TEST_F(CyclesTest, inner_scope) {
  cycles::IntervalGuard ig;
  {
    EXPECT_TRUE(cycles::label(1, 2));
  }
}

TEST_F(CyclesTest, no_interval) {
  EXPECT_FALSE(cycles::label(1, 2));
}

TEST_F(CyclesTest, interval_out_of_scope) {
  {
    cycles::IntervalGuard ig;
  }
  EXPECT_FALSE(cycles::label(1, 2));
}

TEST_F(CyclesTest, multi_threaded) {
  cycles::IntervalGuard ig;

  std::thread t([]() {
    EXPECT_FALSE(cycles::label(1, 2));
  });

  t.join();
}

TEST_F(CyclesTest, already_labeled) {
  cycles::IntervalGuard ig;
  EXPECT_TRUE(cycles::label(1, 2));
  EXPECT_FALSE(cycles::label(5, 6));
}
