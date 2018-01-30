/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <memory>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include <folly/dynamic.h>

#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/routes/ShardSplitRoute.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"
#include "mcrouter/routes/test/ShardSplitRouteTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;
using namespace facebook::memcache::mcrouter::test;

using std::make_shared;
using std::string;
using std::vector;

template <class Request>
void testDirectOp(ShardSplitter splitter) {
  globals::HostidMock hostidMock(1);
  vector<std::shared_ptr<TestHandle>> handles{make_shared<TestHandle>(
      GetRouteTestData(mc_res_found, "a"),
      UpdateRouteTestData(mc_res_found),
      DeleteRouteTestData(mc_res_found))};
  auto rh = get_route_handles(handles)[0];
  McrouterRouteHandle<ShardSplitRoute<McrouterRouterInfo>> splitRoute(
      rh, splitter);

  TestFiberManager fm{FiberManagerContextTag()};
  fm.run([&] {
    mockFiberContext();
    auto reply = splitRoute.route(Request("test:123zz:"));
    EXPECT_EQ(mc_res_found, reply.result());
  });

  EXPECT_EQ(vector<string>{"test:123zz:"}, handles[0]->saw_keys);
}

TEST(shardSplitRoute, simpleSplit_get) {
  ShardSplitter splitter(folly::dynamic::object("123", kNumSplits));
  testShardingForOp<McGetRequest>(splitter);
}

TEST(shardSplitRoute, simpleSplit_set) {
  ShardSplitter splitter(folly::dynamic::object("123", kNumSplits));
  testShardingForOp<McSetRequest>(splitter);
}

TEST(shardSplitRoute, simpleSplit_getDirect) {
  ShardSplitter splitter(folly::dynamic::object("123", kNumSplits));
  testDirectOp<McGetRequest>(splitter);
}

TEST(shardSplitRoute, simpleSplit_setDirect) {
  ShardSplitter splitter(folly::dynamic::object("123", kNumSplits));
  testDirectOp<McGetRequest>(splitter);
}

TEST(shardSplitRoute, simpleSplit_deleteDirect) {
  ShardSplitter splitter(folly::dynamic::object("123", kNumSplits));
  testDirectOp<McDeleteRequest>(splitter);
}

TEST(shardSplitRoute, simpleSplit_deleteFanout) {
  globals::HostidMock hostidMock(1);
  constexpr size_t kNumSplits = 26 * 26 + 1;
  std::vector<std::string> allKeys{"test:123:"};
  for (size_t i = 0; i < kNumSplits - 1; ++i) {
    allKeys.emplace_back(folly::sformat(
        "test:123{}{}:", (char)('a' + i % 26), (char)('a' + i / 26)));
  }

  vector<std::shared_ptr<TestHandle>> handles{
      make_shared<TestHandle>(DeleteRouteTestData(mc_res_found))};
  auto rh = get_route_handles(handles)[0];
  ShardSplitter splitter(folly::dynamic::object("123", kNumSplits));
  McrouterRouteHandle<ShardSplitRoute<McrouterRouterInfo>> splitRoute(
      rh, splitter);

  TestFiberManager fm{FiberManagerContextTag()};
  fm.run([&] {
    mockFiberContext();
    auto reply = splitRoute.route(McDeleteRequest("test:123:"));
    EXPECT_EQ(mc_res_found, reply.result());
  });

  EXPECT_EQ(allKeys, handles[0]->saw_keys);
}

TEST(shardSplitRoute, simpleSplit_deleteNoFanout) {
  ShardSplitter splitter(folly::dynamic::object(
      "123", folly::dynamic::object("new_split_size", kNumSplits)));
  testShardingForOp<McDeleteRequest>(splitter);
}
