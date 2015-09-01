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
#include "mcrouter/routes/FailoverRateLimiter.h"
#include "mcrouter/routes/FailoverRoute.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr
makeFailoverRoute(std::vector<McrouterRouteHandlePtr> rh,
                  FailoverErrorsSettings failoverErrors,
                  std::unique_ptr<FailoverRateLimiter> rateLimiter,
                  bool failoverTagging);
}}}  // facebook::memcache::mcrouter

TEST(failoverRouteTest, success) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(get_route_handles(test_handles),
                              FailoverErrorsSettings(),
                              nullptr,
                              /* failoverTagging */ false);

  auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "a");
}

TEST(failoverRouteTest, once) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(get_route_handles(test_handles),
                              FailoverErrorsSettings(),
                              nullptr,
                              /* failoverTagging */ false);

  auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "b");
}

TEST(failoverRouteTest, twice) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(get_route_handles(test_handles),
                              FailoverErrorsSettings(),
                              nullptr,
                              /* failoverTagging */ false);

  auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "c");
}

TEST(failoverRouteTest, fail) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(get_route_handles(test_handles),
                              FailoverErrorsSettings(),
                              nullptr,
                              /* failoverTagging */ false);

  auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());

  /* Will return the last reply when ran out of targets */
  EXPECT_EQ(toString(reply.value()), "c");
}

TEST(failoverRouteTest, customErrorOnce) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(
    get_route_handles(test_handles),
    FailoverErrorsSettings(std::vector<std::string>{"remote_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "b");
}

TEST(failoverRouteTest, customErrorTwice) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(
    get_route_handles(test_handles),
    FailoverErrorsSettings(std::vector<std::string>{
      "remote_error", "local_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "c");
}

TEST(failoverRouteTest, customErrorUpdate) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_remote_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_local_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_found))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(
    get_route_handles(test_handles),
    FailoverErrorsSettings(std::vector<std::string>{"remote_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequest("0"), McOperation<mc_op_set>());
  EXPECT_TRUE(reply.result() == mc_res_local_error);
}

TEST(failoverRouteTest, separateErrorsGet) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(
    get_route_handles(test_handles),
    FailoverErrorsSettings(
      /* gets */    std::vector<std::string>{"remote_error"},
      /* updates */ std::vector<std::string>{"remote_error", "local_error"},
      /* deletes */ std::vector<std::string>{"local_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_TRUE(toString(reply.value()) == "b");
}

TEST(failoverRouteTest, separateErrorsUpdate) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_remote_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_local_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(
    get_route_handles(test_handles),
    FailoverErrorsSettings(
      /* gets */    std::vector<std::string>{"remote_error"},
      /* updates */ std::vector<std::string>{"remote_error", "local_error"},
      /* deletes */ std::vector<std::string>{"local_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequest("0"), McOperation<mc_op_set>());
  EXPECT_TRUE(reply.result() == mc_res_stored);
}

TEST(failoverRouteTest, separateErrorsDelete) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_local_error)),
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_remote_error)),
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_deleted))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(
    get_route_handles(test_handles),
    FailoverErrorsSettings(
      /* gets */    std::vector<std::string>{"remote_error"},
      /* updates */ std::vector<std::string>{"remote_error", "local_error"},
      /* deletes */ std::vector<std::string>{"local_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequest("0"), McOperation<mc_op_delete>());
  EXPECT_TRUE(reply.result() == mc_res_remote_error);
}

TEST(failoverRouteTest, rateLimit) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b"))
  };

  mockFiberContext();
  auto rh = makeFailoverRoute(
    get_route_handles(test_handles),
    FailoverErrorsSettings(),
    folly::make_unique<FailoverRateLimiter>(0.5, 1),
    /* failoverTagging */ false);

  // tokens: 1
  auto reply1 = rh->route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_EQ(mc_res_found, reply1.result());
  // tokens: 0
  auto reply2 = rh->route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_EQ(mc_res_timeout, reply2.result());
  // tokens: 0.5
  auto reply3 = rh->route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_EQ(mc_res_found, reply3.result());
  // tokens: 0
  auto reply4 = rh->route(McRequest("0"), McOperation<mc_op_get>());
  EXPECT_EQ(mc_res_timeout, reply4.result());
}
