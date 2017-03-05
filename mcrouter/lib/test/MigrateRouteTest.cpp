/*
 *  Copyright (c) 2017, Facebook, Inc.
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

#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/network/gen/Memcache.h"
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

  auto tp_func = []() { return time(nullptr); };
  typedef decltype(tp_func) TimeProviderFunc;

  vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_found, "a"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_deleted)),
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_found, "b"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_notfound)),
  };
  auto route_handles = get_route_handles(test_handles);

  TestFiberManager fm;

  fm.runAll(
      {[&]() { // case 1: curr_time < start_time
         TestRouteHandle<MigrateRoute<TestRouteHandleIf, TimeProviderFunc>> rh(
             route_handles[0],
             route_handles[1],
             curr_time + 25,
             interval,
             tp_func);

         McGetRequest req_get("key_get");
         int cnt = 0;
         RouteHandleTraverser<TestRouteHandleIf> t{
             [&cnt](const TestRouteHandleIf&) { ++cnt; }};
         rh.traverse(req_get, t);
         EXPECT_EQ(1, cnt);

         auto reply_get = rh.route(req_get);
         EXPECT_EQ("a", carbon::valueRangeSlow(reply_get).str());
         EXPECT_EQ(vector<string>{"key_get"}, test_handles[0]->saw_keys);
         EXPECT_NE(vector<string>{"key_get"}, test_handles[1]->saw_keys);
         (test_handles[0]->saw_keys).clear();
         (test_handles[1]->saw_keys).clear();

         McDeleteRequest req_del("key_del");
         cnt = 0;
         rh.traverse(req_del, t);
         EXPECT_EQ(1, cnt);

         auto reply_del = rh.route(req_del);
         EXPECT_EQ(mc_res_deleted, reply_del.result());
         EXPECT_EQ(vector<string>{"key_del"}, test_handles[0]->saw_keys);
         EXPECT_NE(vector<string>{"key_del"}, test_handles[1]->saw_keys);
       },

       [&]() { // case 2: curr_time < start_time + interval
         vector<std::shared_ptr<TestHandle>> test_handles_2{
             make_shared<TestHandle>(
                 GetRouteTestData(mc_res_found, "a"),
                 UpdateRouteTestData(),
                 DeleteRouteTestData(mc_res_deleted)),
             make_shared<TestHandle>(
                 GetRouteTestData(mc_res_notfound, "b"),
                 UpdateRouteTestData(),
                 DeleteRouteTestData(mc_res_notfound)),
         };
         auto route_handles_c2 = get_route_handles(test_handles_2);
         TestRouteHandle<MigrateRoute<TestRouteHandleIf, TimeProviderFunc>> rh(
             route_handles_c2[0],
             route_handles_c2[1],
             curr_time - 25,
             interval,
             tp_func);

         McGetRequest req_get("key_get");
         int cnt = 0;
         RouteHandleTraverser<TestRouteHandleIf> t{
             [&cnt](const TestRouteHandleIf&) { ++cnt; }};
         rh.traverse(req_get, t);
         EXPECT_EQ(cnt, 1);

         auto reply_get = rh.route(req_get);
         EXPECT_EQ("a", carbon::valueRangeSlow(reply_get).str());
         EXPECT_EQ(vector<string>{"key_get"}, test_handles_2[0]->saw_keys);
         EXPECT_NE(vector<string>{"key_get"}, test_handles_2[1]->saw_keys);
         (test_handles_2[0]->saw_keys).clear();
         (test_handles_2[1]->saw_keys).clear();

         McDeleteRequest req_del("key_del");
         cnt = 0;
         rh.traverse(req_del, t);
         EXPECT_EQ(cnt, 2);

         auto reply_del = rh.route(req_del);
         EXPECT_EQ(mc_res_notfound, reply_del.result());
         EXPECT_EQ(vector<string>{"key_del"}, test_handles_2[0]->saw_keys);
         EXPECT_EQ(vector<string>{"key_del"}, test_handles_2[1]->saw_keys);
       },

       [&]() { // case 3: curr_time < start_time + 2*interval
         vector<std::shared_ptr<TestHandle>> test_handles_3{
             make_shared<TestHandle>(
                 GetRouteTestData(mc_res_notfound, "a"),
                 UpdateRouteTestData(),
                 DeleteRouteTestData(mc_res_notfound)),
             make_shared<TestHandle>(
                 GetRouteTestData(mc_res_found, "b"),
                 UpdateRouteTestData(),
                 DeleteRouteTestData(mc_res_deleted)),
         };
         auto route_handles_c3 = get_route_handles(test_handles_3);
         TestRouteHandle<MigrateRoute<TestRouteHandleIf, TimeProviderFunc>> rh(
             route_handles_c3[0],
             route_handles_c3[1],
             curr_time - 75,
             interval,
             tp_func);

         McGetRequest req_get("key_get");
         int cnt = 0;
         RouteHandleTraverser<TestRouteHandleIf> t{
             [&cnt](const TestRouteHandleIf&) { ++cnt; }};
         rh.traverse(req_get, t);
         EXPECT_EQ(1, cnt);

         auto reply_get = rh.route(req_get);
         EXPECT_EQ("b", carbon::valueRangeSlow(reply_get).str());
         EXPECT_NE(vector<string>{"key_get"}, test_handles_3[0]->saw_keys);
         EXPECT_EQ(vector<string>{"key_get"}, test_handles_3[1]->saw_keys);
         (test_handles_3[0]->saw_keys).clear();
         (test_handles_3[1]->saw_keys).clear();

         McDeleteRequest req_del("key_del");
         cnt = 0;
         rh.traverse(req_del, t);
         EXPECT_EQ(2, cnt);

         auto reply_del = rh.route(req_del);
         EXPECT_EQ(mc_res_notfound, reply_del.result());
         EXPECT_EQ(vector<string>{"key_del"}, test_handles_3[0]->saw_keys);
         EXPECT_EQ(vector<string>{"key_del"}, test_handles_3[1]->saw_keys);
       },

       [&]() { // case 4: curr_time > start_time + 2*interval
         TestRouteHandle<MigrateRoute<TestRouteHandleIf, TimeProviderFunc>> rh(
             route_handles[0],
             route_handles[1],
             curr_time - 125,
             interval,
             tp_func);

         McGetRequest req_get("key_get");
         int cnt = 0;
         RouteHandleTraverser<TestRouteHandleIf> t{
             [&cnt](const TestRouteHandleIf&) { ++cnt; }};
         rh.traverse(req_get, t);
         EXPECT_EQ(cnt, 1);

         auto reply_get = rh.route(req_get);
         EXPECT_EQ("b", carbon::valueRangeSlow(reply_get).str());
         EXPECT_NE(vector<string>{"key_get"}, test_handles[0]->saw_keys);
         EXPECT_EQ(vector<string>{"key_get"}, test_handles[1]->saw_keys);
         (test_handles[0]->saw_keys).clear();
         (test_handles[1]->saw_keys).clear();

         McDeleteRequest req_del("key_del");
         cnt = 0;
         rh.traverse(req_del, t);
         EXPECT_EQ(1, cnt);

         auto reply_del = rh.route(req_del);
         EXPECT_EQ(mc_res_notfound, reply_del.result());
         EXPECT_NE(vector<string>{"key_del"}, test_handles[0]->saw_keys);
         EXPECT_EQ(vector<string>{"key_del"}, test_handles[1]->saw_keys);
       }});
}
