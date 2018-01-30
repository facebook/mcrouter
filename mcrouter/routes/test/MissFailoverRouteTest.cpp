/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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

#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/lib/test/TestRouteHandle.h"
#include "mcrouter/routes/MissFailoverRoute.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;
using std::vector;

using TestHandle = TestHandleImpl<TestRouteHandleIf>;

TEST(missMissFailoverRouteTest, success) {
  vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  TestRouteHandle<MissFailoverRoute<TestRouterInfo>> rh(
      get_route_handles(test_handles));

  auto reply = rh.route(McGetRequest("0"));
  EXPECT_EQ("a", carbon::valueRangeSlow(reply).str());
}

TEST(missMissFailoverRouteTest, once) {
  vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  TestRouteHandle<MissFailoverRoute<TestRouterInfo>> rh(
      get_route_handles(test_handles));

  auto reply = rh.route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply).str());
}

TEST(missMissFailoverRouteTest, twice) {
  vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  TestRouteHandle<MissFailoverRoute<TestRouterInfo>> rh(
      get_route_handles(test_handles));

  auto reply = rh.route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());
}

TEST(missMissFailoverRouteTest, fail) {
  vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))};

  TestRouteHandle<MissFailoverRoute<TestRouterInfo>> rh(
      get_route_handles(test_handles));

  auto reply = rh.route(McGetRequest("0"));

  // Should get the last reply
  EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());
  EXPECT_EQ(mc_res_timeout, reply.result());
}

TEST(missMissFailoverRouteTest, bestOnError1) {
  vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))};

  TestRouteHandle<MissFailoverRoute<TestRouterInfo>> rh(
      get_route_handles(test_handles), true);

  auto reply = rh.route(McGetRequest("0"));

  // Should return the first and the only healthy reply
  EXPECT_EQ("a", carbon::valueRangeSlow(reply).str());
  EXPECT_EQ(mc_res_notfound, reply.result());
}

TEST(missMissFailoverRouteTest, bestOnError2) {
  vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))};

  TestRouteHandle<MissFailoverRoute<TestRouterInfo>> rh(
      get_route_handles(test_handles), true);

  auto reply = rh.route(McGetRequest("0"));

  // Should return the only failover-healthy reply
  EXPECT_EQ("b", carbon::valueRangeSlow(reply).str());
  EXPECT_EQ(mc_res_notfound, reply.result());
}

TEST(missMissFailoverRouteTest, bestOnError3) {
  vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "c"))};

  TestRouteHandle<MissFailoverRoute<TestRouterInfo>> rh(
      get_route_handles(test_handles), true);

  auto reply = rh.route(McGetRequest("0"));

  // Should get the LAST healthy reply
  EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());
  EXPECT_EQ(mc_res_notfound, reply.result());
}

TEST(missMissFailoverRouteTest, nonGetLike) {
  vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_notstored)),
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored)),
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))};

  TestRouteHandle<MissFailoverRoute<TestRouterInfo>> rh(
      get_route_handles(test_handles));

  McSetRequest req("0");
  req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "a");
  auto reply = rh.route(std::move(req));

  EXPECT_EQ(mc_res_notstored, reply.result());
  // only first handle sees the key
  EXPECT_EQ(vector<std::string>{"0"}, test_handles[0]->saw_keys);
  EXPECT_TRUE(test_handles[1]->saw_keys.empty());
  EXPECT_TRUE(test_handles[2]->saw_keys.empty());
}
