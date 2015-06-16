/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <ctime>
#include <functional>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/routes/MigrateRoute.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/lib/test/TestRouteHandle.h"

using namespace facebook::memcache;

using std::make_shared;
using std::string;
using std::vector;

using TestHandle = TestHandleImpl<TestRouteHandleIf>;

TEST(migrateRouteTest, migrate) {
  auto curr_time = time(nullptr);
  auto interval = 50;

  auto tp_func =
    [](const McRequest& req) {
      return time(nullptr);
    };
   typedef decltype(tp_func) TimeProviderFunc;

  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a"),
                            UpdateRouteTestData(),
                            DeleteRouteTestData(mc_res_deleted)),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b"),
                            UpdateRouteTestData(),
                            DeleteRouteTestData(mc_res_notfound)),
  };
  auto route_handles = get_route_handles(test_handles);

  TestFiberManager fm;

  fm.runAll({
    [&]() { // case 1: curr_time < start_time
      TestRouteHandle<MigrateRoute<TestRouteHandleIf, TimeProviderFunc>> rh(
        route_handles[0], route_handles[1], curr_time + 25, interval, tp_func);

      McRequest req_get("key_get");
      int cnt = 0;
      RouteHandleTraverser<TestRouteHandleIf> t{
        [&cnt](const TestRouteHandleIf&) { ++cnt; }
      };
      rh.traverse(req_get, McOperation<mc_op_get>(), t);
      EXPECT_EQ(cnt, 1);

      auto reply_get = rh.route(req_get, McOperation<mc_op_get>());
      EXPECT_TRUE(toString(reply_get.value()) == "a");
      EXPECT_TRUE(test_handles[0]->saw_keys == vector<string>{"key_get"});
      EXPECT_FALSE(test_handles[1]->saw_keys == vector<string>{"key_get"});
      (test_handles[0]->saw_keys).clear();
      (test_handles[1]->saw_keys).clear();

      McRequest req_del("key_del");
      cnt = 0;
      rh.traverse(req_del, McOperation<mc_op_delete>(), t);
      EXPECT_EQ(cnt, 1);

      auto reply_del = rh.route(req_del, McOperation<mc_op_delete>());
      EXPECT_TRUE(reply_del.result() == mc_res_deleted);
      EXPECT_TRUE(test_handles[0]->saw_keys == vector<string>{"key_del"});
      EXPECT_FALSE(test_handles[1]->saw_keys == vector<string>{"key_del"});
    },

    [&]() { // case 2: curr_time < start_time + interval
      vector<std::shared_ptr<TestHandle>> test_handles{
        make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a"),
                                UpdateRouteTestData(),
                                DeleteRouteTestData(mc_res_deleted)),
        make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b"),
                                UpdateRouteTestData(),
                                DeleteRouteTestData(mc_res_notfound)),
      };
      auto route_handles_c2 = get_route_handles(test_handles);
      TestRouteHandle<MigrateRoute<TestRouteHandleIf, TimeProviderFunc>> rh(
        route_handles_c2[0], route_handles_c2[1],
        curr_time -  25, interval, tp_func);

      McRequest req_get("key_get");
      int cnt = 0;
      RouteHandleTraverser<TestRouteHandleIf> t{
        [&cnt](const TestRouteHandleIf&) { ++cnt; }
      };
      rh.traverse(req_get, McOperation<mc_op_get>(), t);
      EXPECT_EQ(cnt, 1);

      auto reply_get = rh.route(req_get, McOperation<mc_op_get>());
      EXPECT_TRUE(toString(reply_get.value()) == "a");
      EXPECT_TRUE(test_handles[0]->saw_keys == vector<string>{"key_get"});
      EXPECT_FALSE(test_handles[1]->saw_keys == vector<string>{"key_get"});
      (test_handles[0]->saw_keys).clear();
      (test_handles[1]->saw_keys).clear();

      McRequest req_del("key_del");
      cnt = 0;
      rh.traverse(req_del, McOperation<mc_op_delete>(), t);
      EXPECT_EQ(cnt, 2);

      auto reply_del = rh.route(req_del, McOperation<mc_op_delete>());
      EXPECT_TRUE(reply_del.result() == mc_res_notfound);
      EXPECT_TRUE(test_handles[0]->saw_keys == vector<string>{"key_del"});
      EXPECT_TRUE(test_handles[1]->saw_keys == vector<string>{"key_del"});
    },

    [&]() { // case 3: curr_time < start_time + 2*interval
      vector<std::shared_ptr<TestHandle>> test_handles{
        make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a"),
                                UpdateRouteTestData(),
                                DeleteRouteTestData(mc_res_notfound)),
        make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b"),
                                UpdateRouteTestData(),
                                DeleteRouteTestData(mc_res_deleted)),
      };
      auto route_handles_c3 = get_route_handles(test_handles);
      TestRouteHandle<MigrateRoute<TestRouteHandleIf, TimeProviderFunc>> rh(
        route_handles_c3[0], route_handles_c3[1],
        curr_time - 75, interval, tp_func);

      McRequest req_get("key_get");
      int cnt = 0;
      RouteHandleTraverser<TestRouteHandleIf> t{
        [&cnt](const TestRouteHandleIf&) { ++cnt; }
      };
      rh.traverse(req_get, McOperation<mc_op_get>(), t);
      EXPECT_EQ(cnt, 1);

      auto reply_get = rh.route(McRequest("key_get"), McOperation<mc_op_get>());
      EXPECT_TRUE(toString(reply_get.value()) == "b");
      EXPECT_FALSE(test_handles[0]->saw_keys == vector<string>{"key_get"});
      EXPECT_TRUE(test_handles[1]->saw_keys == vector<string>{"key_get"});
      (test_handles[0]->saw_keys).clear();
      (test_handles[1]->saw_keys).clear();

      McRequest req_del("key_del");
      cnt = 0;
      rh.traverse(req_del, McOperation<mc_op_delete>(), t);
      EXPECT_EQ(cnt, 2);

      auto reply_del = rh.route(req_del, McOperation<mc_op_delete>());
      EXPECT_TRUE(reply_del.result() == mc_res_notfound);
      EXPECT_TRUE(test_handles[0]->saw_keys == vector<string>{"key_del"});
      EXPECT_TRUE(test_handles[1]->saw_keys == vector<string>{"key_del"});
    },

    [&]() { // case 4: curr_time > start_time + 2*interval
      TestRouteHandle<MigrateRoute<TestRouteHandleIf, TimeProviderFunc>> rh(
        route_handles[0], route_handles[1], curr_time - 125, interval, tp_func);

      McRequest req_get("key_get");
      int cnt = 0;
      RouteHandleTraverser<TestRouteHandleIf> t{
        [&cnt](const TestRouteHandleIf&) { ++cnt; }
      };
      rh.traverse(req_get, McOperation<mc_op_get>(), t);
      EXPECT_EQ(cnt, 1);

      auto reply_get = rh.route(req_get, McOperation<mc_op_get>());
      EXPECT_TRUE(toString(reply_get.value()) == "b");
      EXPECT_FALSE(test_handles[0]->saw_keys == vector<string>{"key_get"});
      EXPECT_TRUE(test_handles[1]->saw_keys == vector<string>{"key_get"});
      (test_handles[0]->saw_keys).clear();
      (test_handles[1]->saw_keys).clear();

      McRequest req_del("key_del");
      cnt = 0;
      rh.traverse(req_del, McOperation<mc_op_delete>(), t);
      EXPECT_EQ(cnt, 1);

      auto reply_del = rh.route(req_del, McOperation<mc_op_delete>());
      EXPECT_TRUE(reply_del.result() == mc_res_notfound);
      EXPECT_FALSE(test_handles[0]->saw_keys == vector<string>{"key_del"});
      EXPECT_TRUE(test_handles[1]->saw_keys == vector<string>{"key_del"});
    }
  });
}
