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

#include "mcrouter/lib/carbon/example/gen/HelloGoodbyeRouterInfo.h"

TEST(HelloGoodbye, getTypeIdByName) {
  EXPECT_EQ(
      carbon::getTypeIdByName(
          "hello", hellogoodbye::HelloGoodbyeRouterInfo::RoutableRequests()),
      65);
  EXPECT_EQ(
      carbon::getTypeIdByName(
          "hello1", hellogoodbye::HelloGoodbyeRouterInfo::RoutableRequests()),
      0);
}
