/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/routes/RandomRoute.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/lib/test/TestRouteHandle.h"

using namespace facebook::memcache;

using std::make_shared;
using std::vector;

using TestHandle = TestHandleImpl<TestRouteHandleIf>;

TEST(randomRouteTest, success) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
  };

  TestRouteHandle<RandomRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(reply.isHit());
}

TEST(randomRouteTest, cover) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
  };

  TestRouteHandle<RandomRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  int hit = 0, miss = 0;
  const int rounds = 32;
  for (int i = 0; i < rounds; i++) {
    auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
    hit += reply.isHit();
    miss += reply.isMiss();
  }

  EXPECT_GT(hit, 0);
  EXPECT_GT(miss, 0);
  EXPECT_EQ(hit + miss, rounds);
}

TEST(randomRouteTest, fail) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "c")),
  };

  TestRouteHandle<RandomRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());

  EXPECT_TRUE(!reply.isHit());
}
