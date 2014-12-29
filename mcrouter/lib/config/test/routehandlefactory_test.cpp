/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/config/test/TestRouteHandleProvider.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;

TEST(RouteHandleFactoryTest, sanity) {
  TestFiberManager fm;

  RouteHandleProvider<TestRouteHandleIf> provider;

  RouteHandleFactory<TestRouteHandleIf> factory(provider);

  auto rh = factory.create("AllAsyncRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_EQ(reply.result(), mc_res_notfound);
  });

  rh = factory.create("AllFastestRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("AllInitialRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("AllMajorityRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("AllSyncRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("FailoverRoute|NullRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_EQ(reply.result(), mc_res_notfound);
  });

  rh = factory.create("HashRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("HostIdRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("LatestRoute|NullRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_EQ(reply.result(), mc_res_notfound);
  });

  rh = factory.create("MissFailoverRoute|NullRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_EQ(reply.result(), mc_res_notfound);
  });

  rh = factory.create("RandomRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });
}
