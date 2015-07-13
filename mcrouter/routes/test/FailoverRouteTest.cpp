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
#include "mcrouter/routes/FailoverRoute.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;
using std::vector;

using TestHandle = TestHandleImpl<TestRouteHandleIf>;

TEST(failoverRouteTest, success) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<FailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles), FailoverErrorsSettings());

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "a");
}

TEST(failoverRouteTest, once) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<FailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles), FailoverErrorsSettings());

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "b");
}

TEST(failoverRouteTest, twice) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<FailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles), FailoverErrorsSettings());

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "c");
}

TEST(failoverRouteTest, fail) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))
  };

  TestRouteHandle<FailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles), FailoverErrorsSettings());

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());

  /* Will return the last reply when ran out of targets */
  EXPECT_EQ(toString(reply.value()), "c");
}

TEST(failoverRouteTest, customErrorOnce) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<FailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles),
    FailoverErrorsSettings(std::vector<std::string>{"remote_error"}));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "b");
}

TEST(failoverRouteTest, customErrorTwice) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<FailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles),
    FailoverErrorsSettings(std::vector<std::string>{
      "remote_error", "local_error"}));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "c");
}

TEST(failoverRouteTest, customErrorUpdate) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_remote_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_local_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_found))
  };

  TestRouteHandle<FailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles),
    FailoverErrorsSettings(std::vector<std::string>{"remote_error"}));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_set>());
  EXPECT_TRUE(reply.result() == mc_res_local_error);
}

TEST(failoverRouteTest, separateErrorsGet) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<FailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles),
    FailoverErrorsSettings(
      /* gets */    std::vector<std::string>{"remote_error"},
      /* updates */ std::vector<std::string>{"remote_error", "local_error"},
      /* deletes */ std::vector<std::string>{"local_error"}));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "b");
}

TEST(failoverRouteTest, separateErrorsUpdate) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_remote_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_local_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))
  };

  TestRouteHandle<FailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles),
    FailoverErrorsSettings(
      /* gets */    std::vector<std::string>{"remote_error"},
      /* updates */ std::vector<std::string>{"remote_error", "local_error"},
      /* deletes */ std::vector<std::string>{"local_error"}));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_set>());
  EXPECT_TRUE(reply.result() == mc_res_stored);
}

TEST(failoverRouteTest, separateErrorsDelete) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_local_error)),
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_remote_error)),
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_deleted))
  };

  TestRouteHandle<FailoverRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles),
    FailoverErrorsSettings(
      /* gets */    std::vector<std::string>{"remote_error"},
      /* updates */ std::vector<std::string>{"remote_error", "local_error"},
      /* deletes */ std::vector<std::string>{"local_error"}));

  auto reply = rh.route(McRequest("0"), McOperation<mc_op_delete>());
  EXPECT_TRUE(reply.result() == mc_res_remote_error);
}
