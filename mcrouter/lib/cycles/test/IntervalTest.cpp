/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <string>

#include <gtest/gtest.h>

#include "mcrouter/lib/cycles/Clocks.h"
#include "mcrouter/lib/cycles/Interval.h"

using namespace facebook::memcache::cycles;
using namespace facebook::memcache::cycles::detail;

TEST(Interval, basic) {
  const uint64_t length = 123;
  const uint64_t contextSwitches = 3;
  const size_t reqType = 1;
  const uint64_t reqId = 2;

  Interval interval(Metering{length, contextSwitches},
                    IntervalLabel(reqType, reqId));

  EXPECT_EQ(length, interval.length());
  EXPECT_EQ(contextSwitches, interval.contextSwitches());
  EXPECT_EQ(reqType, interval.label().requestType());
  EXPECT_EQ(reqId, interval.label().requestId());
}
