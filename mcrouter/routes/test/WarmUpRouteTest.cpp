/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <functional>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/lib/test/TestRouteHandle.h"
#include "mcrouter/routes/WarmUpRoute.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;
using std::string;
using std::vector;

using TestHandle = TestHandleImpl<TestRouteHandleIf>;

TEST(warmUpRouteTest, warmUp) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a"),
                            UpdateRouteTestData(mc_res_stored),
                            DeleteRouteTestData(mc_res_deleted)),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b"),
                            UpdateRouteTestData(mc_res_stored),
                            DeleteRouteTestData(mc_res_notfound)),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, ""),
                            UpdateRouteTestData(mc_res_notstored),
                            DeleteRouteTestData(mc_res_notfound)),
  };
  auto route_handles = get_route_handles(test_handles);

  TestFiberManager fm;

  fm.run([&]() {
    TestRouteHandle<WarmUpRoute<TestRouteHandleIf>> rh(
      route_handles[0], route_handles[1], 1);

    auto reply_get = rh.route(McRequest("key_get"), McOperation<mc_op_get>());
    EXPECT_TRUE("b" == toString(reply_get.value()));
    EXPECT_TRUE(vector<string>{"key_get"} != test_handles[0]->saw_keys);
    EXPECT_TRUE(vector<string>{"key_get"} == test_handles[1]->saw_keys);
    (test_handles[0]->saw_keys).clear();
    (test_handles[1]->saw_keys).clear();

    auto reply_del = rh.route(McRequest("key_del"),
                              McOperation<mc_op_delete>());
    EXPECT_TRUE(mc_res_notfound == reply_del.result());
    EXPECT_TRUE(vector<string>{"key_del"} != test_handles[0]->saw_keys);
    EXPECT_TRUE(vector<string>{"key_del"} == test_handles[1]->saw_keys);
  });
  fm.run([&]() {
    TestRouteHandle<WarmUpRoute<TestRouteHandleIf>> rh(
      route_handles[0], route_handles[2], 1);

    auto reply_get = rh.route(McRequest("key_get"), McOperation<mc_op_get>());
    EXPECT_TRUE("a" == toString(reply_get.value()));
    EXPECT_TRUE(vector<string>{"key_get"} == test_handles[0]->saw_keys);
    EXPECT_TRUE(vector<string>{"key_get"} == test_handles[2]->saw_keys);
  });
  fm.run([&]() {
    EXPECT_TRUE((vector<uint32_t>{0, 1}) == test_handles[2]->sawExptimes);
    (test_handles[0]->saw_keys).clear();
    (test_handles[2]->saw_keys).clear();
    EXPECT_TRUE((vector<mc_op_t>{ mc_op_get, mc_op_add }) ==
              test_handles[2]->sawOperations);
  });
  fm.run([&]() {
    TestRouteHandle<WarmUpRoute<TestRouteHandleIf>> rh(
      route_handles[0], route_handles[2], 1);

    auto reply_del = rh.route(McRequest("key_del"),
                              McOperation<mc_op_delete>());
    EXPECT_TRUE(mc_res_notfound == reply_del.result());
    EXPECT_TRUE(vector<string>{"key_del"} != test_handles[0]->saw_keys);
    EXPECT_TRUE(vector<string>{"key_del"} == test_handles[2]->saw_keys);
  });


}
