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

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "a");
}

TEST(missMissFailoverRouteTest, once) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<MissFailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "b");
}

TEST(missMissFailoverRouteTest, twice) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<MissFailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "c");
}

TEST(missMissFailoverRouteTest, fail) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))
  };

  TestRouteHandle<MissFailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());

  // Should get the last reply
  EXPECT_TRUE(toString(reply.value()) == "c");
  EXPECT_TRUE(reply.result() == mc_res_timeout);
}

TEST(missMissFailoverRouteTest, nonGetLike) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_notstored)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))
  };

  TestRouteHandle<MissFailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  auto msg = createMcMsgRef("0", "a");
  auto reply = rh.route(McRequest(std::move(msg)), McOperation<mc_op_set>());
  EXPECT_TRUE(toString(reply.value()) == "a");
  // only first handle sees the key
  EXPECT_TRUE(test_handles[0]->saw_keys == vector<std::string>{"0"});
  EXPECT_TRUE(test_handles[1]->saw_keys.size() == 0);
  EXPECT_TRUE(test_handles[2]->saw_keys.size() == 0);
}
