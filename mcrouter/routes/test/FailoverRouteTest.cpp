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

#include <folly/dynamic.h>

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
makeFailoverRouteInOrder(std::vector<McrouterRouteHandlePtr> rh,
                         FailoverErrorsSettings failoverErrors,
                         std::unique_ptr<FailoverRateLimiter> rateLimiter,
                         bool failoverTagging,
                         bool enableLeasePairing = false,
                         std::string name = "");

McrouterRouteHandlePtr
makeFailoverRouteLeastFailures(std::vector<McrouterRouteHandlePtr> rh,
                               FailoverErrorsSettings failoverErrors,
                               std::unique_ptr<FailoverRateLimiter> rateLimiter,
                               bool failoverTagging,
                               bool enableLeasePairing,
                               std::string name,
                               const folly::dynamic& json);

}}}  // facebook::memcache::mcrouter

TEST(failoverRouteTest, success) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(get_route_handles(test_handles),
                                     FailoverErrorsSettings(),
                                     nullptr,
                                     /* failoverTagging */ false);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_TRUE(toString(reply.value()) == "a");
}

TEST(failoverRouteTest, once) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(get_route_handles(test_handles),
                                     FailoverErrorsSettings(),
                                     nullptr,
                                     /* failoverTagging */ false);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_TRUE(toString(reply.value()) == "b");
}

TEST(failoverRouteTest, twice) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(get_route_handles(test_handles),
                                     FailoverErrorsSettings(),
                                     nullptr,
                                     /* failoverTagging */ false);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_TRUE(toString(reply.value()) == "c");
}

TEST(failoverRouteTest, fail) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(get_route_handles(test_handles),
                                     FailoverErrorsSettings(),
                                     nullptr,
                                     /* failoverTagging */ false);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));

  /* Will return the last reply when ran out of targets */
  EXPECT_EQ("c", toString(reply.value()));
}

TEST(failoverRouteTest, customErrorOnce) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
    get_route_handles(test_handles),
    FailoverErrorsSettings(std::vector<std::string>{"remote_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_TRUE(toString(reply.value()) == "b");
}

TEST(failoverRouteTest, customErrorTwice) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
    get_route_handles(test_handles),
    FailoverErrorsSettings(std::vector<std::string>{
      "remote_error", "local_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_TRUE(toString(reply.value()) == "c");
}

TEST(failoverRouteTest, customErrorUpdate) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_remote_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_local_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_found))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
    get_route_handles(test_handles),
    FailoverErrorsSettings(std::vector<std::string>{"remote_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequestWithMcOp<mc_op_set>("0"));
  EXPECT_TRUE(reply.result() == mc_res_local_error);
}

TEST(failoverRouteTest, separateErrorsGet) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
    get_route_handles(test_handles),
    FailoverErrorsSettings(
      /* gets */    std::vector<std::string>{"remote_error"},
      /* updates */ std::vector<std::string>{"remote_error", "local_error"},
      /* deletes */ std::vector<std::string>{"local_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_TRUE(toString(reply.value()) == "b");
}

TEST(failoverRouteTest, separateErrorsUpdate) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_remote_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_local_error)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
    get_route_handles(test_handles),
    FailoverErrorsSettings(
      /* gets */    std::vector<std::string>{"remote_error"},
      /* updates */ std::vector<std::string>{"remote_error", "local_error"},
      /* deletes */ std::vector<std::string>{"local_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply1 = rh->route(McRequestWithMcOp<mc_op_set>("0"));
  EXPECT_TRUE(reply1.result() == mc_res_stored);
  auto reply2 = rh->route(McRequestWithMcOp<mc_op_append>("0"));
  EXPECT_TRUE(reply2.result() == mc_res_stored);
  auto reply3 = rh->route(McRequestWithMcOp<mc_op_prepend>("0"));
  EXPECT_TRUE(reply3.result() == mc_res_stored);
}

TEST(failoverRouteTest, separateErrorsDelete) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_local_error)),
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_remote_error)),
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_deleted))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
    get_route_handles(test_handles),
    FailoverErrorsSettings(
      /* gets */    std::vector<std::string>{"remote_error"},
      /* updates */ std::vector<std::string>{"remote_error", "local_error"},
      /* deletes */ std::vector<std::string>{"local_error"}),
    nullptr,
    /* failoverTagging */ false);

  auto reply = rh->route(McRequestWithMcOp<mc_op_delete>("0"));
  EXPECT_TRUE(reply.result() == mc_res_remote_error);
}

TEST(failoverRouteTest, rateLimit) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b"))
  };

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
    get_route_handles(test_handles),
    FailoverErrorsSettings(),
    folly::make_unique<FailoverRateLimiter>(0.5, 1),
    /* failoverTagging */ false);

  // tokens: 1
  auto reply1 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ(mc_res_found, reply1.result());
  // tokens: 0
  auto reply2 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ(mc_res_timeout, reply2.result());
  // tokens: 0.5
  auto reply3 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ(mc_res_found, reply3.result());
  // tokens: 0
  auto reply4 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ(mc_res_timeout, reply4.result());
}

TEST(failoverRouteTest, leastFailuresNoFailover) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  folly::dynamic json = folly::dynamic::object ("type", "LeastFailuresPolicy")
                                               ("max_tries", 2);
  auto rh = makeFailoverRouteLeastFailures(get_route_handles(test_handles),
                                           FailoverErrorsSettings(),
                                           nullptr,
                                           /* failoverTagging */ false,
                                           /* enableLeasePairing */ false,
                                           "route01",
                                           json);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("a", toString(reply.value()));
}

TEST(failoverRouteTest, leastFailuresFailoverOnce) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  folly::dynamic json = folly::dynamic::object ("type", "LeastFailuresPolicy")
                                               ("max_tries", 3);
  auto rh = makeFailoverRouteLeastFailures(get_route_handles(test_handles),
                                           FailoverErrorsSettings(),
                                           nullptr,
                                           /* failoverTagging */ false,
                                           /* enableLeasePairing */ false,
                                           "route01",
                                           json);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("b", toString(reply.value()));
}

TEST(failoverRouteTest, leastFailuresFailoverTwice) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  mockFiberContext();
  folly::dynamic json = folly::dynamic::object ("type", "LeastFailuresPolicy")
                                               ("max_tries", 3);
  auto rh = makeFailoverRouteLeastFailures(get_route_handles(test_handles),
                                           FailoverErrorsSettings(),
                                           nullptr,
                                           /* failoverTagging */ false,
                                           /* enableLeasePairing */ false,
                                           "route01",
                                           json);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("c", toString(reply.value()));
}

TEST(failoverRouteTest, leastFailuresLastSucceeds) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d"))
  };

  mockFiberContext();
  folly::dynamic json = folly::dynamic::object ("type", "LeastFailuresPolicy")
                                               ("max_tries", 2);
  auto rh = makeFailoverRouteLeastFailures(get_route_handles(test_handles),
                                           FailoverErrorsSettings(),
                                           nullptr,
                                           /* failoverTagging */ false,
                                           /* enableLeasePairing */ false,
                                           "route01",
                                           json);

  auto reply1= rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("b", toString(reply1.value()));

  auto reply2 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("c", toString(reply2.value()));

  auto reply3 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("d", toString(reply3.value()));

  auto reply4 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("d", toString(reply4.value()));

  auto reply5 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("d", toString(reply5.value()));
}

TEST(failoverRouteTest, leastFailuresCycle) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "d"))
  };

  mockFiberContext();
  folly::dynamic json = folly::dynamic::object ("type", "LeastFailuresPolicy")
                                               ("max_tries", 2);
  auto rh = makeFailoverRouteLeastFailures(get_route_handles(test_handles),
                                           FailoverErrorsSettings(),
                                           nullptr,
                                           /* failoverTagging */ false,
                                           /* enableLeasePairing */ false,
                                           "route01",
                                           json);

  auto reply1= rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("b", toString(reply1.value()));

  auto reply2 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("c", toString(reply2.value()));

  auto reply3 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("d", toString(reply3.value()));

  auto reply4 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("b", toString(reply4.value()));

  auto reply5 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("c", toString(reply5.value()));

  auto reply6 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("d", toString(reply6.value()));
}

TEST(failoverRouteTest, leastFailuresFailAll) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))
  };

  mockFiberContext();
  folly::dynamic json = folly::dynamic::object ("type", "LeastFailuresPolicy")
                                               ("max_tries", 3);
  auto rh = makeFailoverRouteLeastFailures(get_route_handles(test_handles),
                                           FailoverErrorsSettings(),
                                           nullptr,
                                           /* failoverTagging */ false,
                                           /* enableLeasePairing */ false,
                                           "route01",
                                           json);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("c", toString(reply.value()));
}

TEST(failoverRouteTest, leastFailuresFailAllLimit) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))
  };

  mockFiberContext();
  folly::dynamic json = folly::dynamic::object ("type", "LeastFailuresPolicy")
                                               ("max_tries", 2);
  auto rh = makeFailoverRouteLeastFailures(get_route_handles(test_handles),
                                           FailoverErrorsSettings(),
                                           nullptr,
                                           /* failoverTagging */ false,
                                           /* enableLeasePairing */ false,
                                           "route01",
                                           json);

  auto reply = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("b", toString(reply.value()));
}

TEST(failoverRouteTest, leastFailuresComplex) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a"),
                            UpdateRouteTestData(mc_res_timeout),
                            DeleteRouteTestData()),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b"),
                            UpdateRouteTestData(mc_res_stored),
                            DeleteRouteTestData()),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"),
                            UpdateRouteTestData(mc_res_timeout),
                            DeleteRouteTestData())
  };

  mockFiberContext();
  folly::dynamic json = folly::dynamic::object ("type", "LeastFailuresPolicy")
                                               ("max_tries", 2);
  auto rh = makeFailoverRouteLeastFailures(get_route_handles(test_handles),
                                           FailoverErrorsSettings(),
                                           nullptr,
                                           /* failoverTagging */ false,
                                           /* enableLeasePairing */ false,
                                           "route01",
                                           json);

  auto reply1 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("b", toString(reply1.value()));

  auto reply2 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("c", toString(reply2.value()));

  auto reply3 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("b", toString(reply3.value()));

  // At this point, b has failed 2 times, c has failed 1 time
  // Next request is routed to c
  rh->route(McRequestWithMcOp<mc_op_set>("0"));

  // Now both b and c have error count 2.  Next request routed to b.
  // b's error count will be reset to 0 on success
  // c still has error count 2
  rh->route(McRequestWithMcOp<mc_op_set>("0"));

  // Fail b twice
  auto reply4 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("b", toString(reply4.value()));
  auto reply5 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("b", toString(reply5.value()));

  // Now b and c have same error count (2)
  auto reply6 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("b", toString(reply6.value()));

  auto reply7 = rh->route(McRequestWithMcOp<mc_op_get>("0"));
  EXPECT_EQ("c", toString(reply7.value()));
}
