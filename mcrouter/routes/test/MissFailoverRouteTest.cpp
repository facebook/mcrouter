/*
 *  Copyright (c) 2016, Facebook, Inc.
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

#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
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
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<MissFailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  auto reply = rh.route(TypedThriftRequest<cpp2::McGetRequest>("0"));
  EXPECT_EQ("a", reply.valueRangeSlow().str());
}

TEST(missMissFailoverRouteTest, once) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<MissFailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  auto reply = rh.route(TypedThriftRequest<cpp2::McGetRequest>("0"));
  EXPECT_EQ("b", reply.valueRangeSlow().str());
}

TEST(missMissFailoverRouteTest, twice) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<MissFailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  auto reply = rh.route(TypedThriftRequest<cpp2::McGetRequest>("0"));
  EXPECT_EQ("c", reply.valueRangeSlow().str());
}

TEST(missMissFailoverRouteTest, fail) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))
  };

  TestRouteHandle<MissFailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  auto reply = rh.route(TypedThriftRequest<cpp2::McGetRequest>("0"));

  // Should get the last reply
  EXPECT_EQ("c", reply.valueRangeSlow().str());
  EXPECT_EQ(mc_res_timeout, reply.result());
}

TEST(missMissFailoverRouteTest, nonGetLike) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_notstored)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))
  };

  TestRouteHandle<MissFailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  TypedThriftRequest<cpp2::McSetRequest> req("0");
  req.setValue("a");
  auto reply = rh.route(std::move(req));

  EXPECT_EQ(mc_res_notstored, reply.result());
  // only first handle sees the key
  EXPECT_EQ(vector<std::string>{"0"}, test_handles[0]->saw_keys);
  EXPECT_TRUE(test_handles[1]->saw_keys.empty());
  EXPECT_TRUE(test_handles[2]->saw_keys.empty());
}
