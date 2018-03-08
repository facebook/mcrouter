/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */

#include <gtest/gtest.h>

#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/lib/test/TestRouteHandle.h"
#include "mcrouter/routes/ErrorRoute.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

TEST(ErrorRoute, create) {
  TestRouteHandle<ErrorRoute<TestRouterInfo>> rh;
  EXPECT_EQ("error", rh.routeName());
}

TEST(ErrorRoute, createCustomMessage) {
  TestRouteHandle<ErrorRoute<TestRouterInfo>> rh("custom msg");
  EXPECT_EQ("error|custom msg", rh.routeName());
}

TEST(ErrorRoute, route) {
  TestRouteHandle<ErrorRoute<TestRouterInfo>> rh;
  auto reply = rh.route(McGetRequest("key"));
  EXPECT_TRUE(isErrorResult(reply.result()));
}

TEST(ErrorRoute, routeCustomMessage) {
  TestRouteHandle<ErrorRoute<TestRouterInfo>> rh("custom msg");
  auto reply = rh.route(McGetRequest("key"));
  EXPECT_TRUE(isErrorResult(reply.result()));
  EXPECT_EQ("custom msg", reply.message());
}
