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

#include <folly/dynamic.h>

#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/lib/test/TestRouteHandle.h"
#include "mcrouter/routes/FailoverRateLimiter.h"
#include "mcrouter/routes/FailoverRoute.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;

namespace facebook {
namespace memcache {
namespace mcrouter {

McrouterRouteHandlePtr makeFailoverRouteInOrder(
    std::vector<McrouterRouteHandlePtr> rh,
    FailoverErrorsSettings failoverErrors,
    std::unique_ptr<FailoverRateLimiter> rateLimiter,
    bool failoverTagging,
    bool enableLeasePairing = false,
    std::string name = "") {
  return makeFailoverRouteInOrder<McrouterRouterInfo, FailoverRoute>(
      std::move(rh),
      std::move(failoverErrors),
      std::move(rateLimiter),
      failoverTagging,
      enableLeasePairing,
      std::move(name),
      nullptr);
}
}
}
} // facebook::memcache::mcrouter

TEST(failoverRouteTest, nofailover) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("a", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, success) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("a", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, once) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, twice) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, fail) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false);

  auto reply = rh->route(McGetRequest("0"));

  /* Will return the last reply when ran out of targets */
  EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, customErrorOnce) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(std::vector<std::string>{"remote_error"}),
      nullptr,
      /* failoverTagging */ false);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, customErrorTwice) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(
          std::vector<std::string>{"remote_error", "local_error"}),
      nullptr,
      /* failoverTagging */ false);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, customErrorUpdate) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_remote_error)),
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_local_error)),
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_found))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(std::vector<std::string>{"remote_error"}),
      nullptr,
      /* failoverTagging */ false);

  McSetRequest req("0");
  req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "value");
  auto reply = rh->route(std::move(req));
  EXPECT_EQ(mc_res_local_error, reply.result());
}

TEST(failoverRouteTest, separateErrorsGet) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_local_error, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(
          /* gets */ std::vector<std::string>{"remote_error"},
          /* updates */ std::vector<std::string>{"remote_error", "local_error"},
          /* deletes */ std::vector<std::string>{"local_error"}),
      nullptr,
      /* failoverTagging */ false);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, separateErrorsUpdate) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_remote_error)),
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_local_error)),
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(
          /* gets */ std::vector<std::string>{"remote_error"},
          /* updates */ std::vector<std::string>{"remote_error", "local_error"},
          /* deletes */ std::vector<std::string>{"local_error"}),
      nullptr,
      /* failoverTagging */ false);

  {
    McSetRequest req("0");
    req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "value");
    auto reply1 = rh->route(std::move(req));
    EXPECT_EQ(mc_res_stored, reply1.result());
  }
  {
    McAppendRequest req("0");
    req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "value");
    auto reply2 = rh->route(std::move(req));
    EXPECT_EQ(mc_res_stored, reply2.result());
  }
  {
    McPrependRequest req("0");
    req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "value");
    auto reply3 = rh->route(std::move(req));
    EXPECT_EQ(mc_res_stored, reply3.result());
  }
}

TEST(failoverRouteTest, separateErrorsDelete) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(DeleteRouteTestData(mc_res_local_error)),
      make_shared<TestHandle>(DeleteRouteTestData(mc_res_remote_error)),
      make_shared<TestHandle>(DeleteRouteTestData(mc_res_deleted))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(
          /* gets */ std::vector<std::string>{"remote_error"},
          /* updates */ std::vector<std::string>{"remote_error", "local_error"},
          /* deletes */ std::vector<std::string>{"local_error"}),
      nullptr,
      /* failoverTagging */ false);

  auto reply = rh->route(McDeleteRequest("0"));
  EXPECT_EQ(mc_res_remote_error, reply.result());
}

TEST(failoverRouteTest, rateLimit) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      std::make_unique<FailoverRateLimiter>(0.5, 1),
      /* failoverTagging */ false);

  // tokens: 1
  auto reply1 = rh->route(McGetRequest("0"));
  EXPECT_EQ(mc_res_found, reply1.result());
  // tokens: 0
  auto reply2 = rh->route(McGetRequest("0"));
  EXPECT_EQ(mc_res_timeout, reply2.result());
  // tokens: 0.5
  auto reply3 = rh->route(McGetRequest("0"));
  EXPECT_EQ(mc_res_found, reply3.result());
  // tokens: 0
  auto reply4 = rh->route(McGetRequest("0"));
  EXPECT_EQ(mc_res_timeout, reply4.result());
}

TEST(failoverRouteTest, leastFailuresNoFailover) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  mockFiberContext();
  folly::dynamic json =
      folly::dynamic::object("type", "LeastFailuresPolicy")("max_tries", 2);
  auto rh = makeFailoverRouteLeastFailures<McrouterRouterInfo, FailoverRoute>(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false,
      /* enableLeasePairing */ false,
      "route01",
      json);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("a", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, leastFailuresFailoverOnce) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  mockFiberContext();
  folly::dynamic json =
      folly::dynamic::object("type", "LeastFailuresPolicy")("max_tries", 3);
  auto rh = makeFailoverRouteLeastFailures<McrouterRouterInfo, FailoverRoute>(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false,
      /* enableLeasePairing */ false,
      "route01",
      json);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, leastFailuresFailoverTwice) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  mockFiberContext();
  folly::dynamic json =
      folly::dynamic::object("type", "LeastFailuresPolicy")("max_tries", 3);
  auto rh = makeFailoverRouteLeastFailures<McrouterRouterInfo, FailoverRoute>(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false,
      /* enableLeasePairing */ false,
      "route01",
      json);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, leastFailuresLastSucceeds) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d"))};

  mockFiberContext();
  folly::dynamic json =
      folly::dynamic::object("type", "LeastFailuresPolicy")("max_tries", 2);
  auto rh = makeFailoverRouteLeastFailures<McrouterRouterInfo, FailoverRoute>(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false,
      /* enableLeasePairing */ false,
      "route01",
      json);

  auto reply1 = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply1).str());

  auto reply2 = rh->route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply2).str());

  auto reply3 = rh->route(McGetRequest("0"));
  EXPECT_EQ("d", carbon::valueRangeSlow(reply3).str());

  auto reply4 = rh->route(McGetRequest("0"));
  EXPECT_EQ("d", carbon::valueRangeSlow(reply4).str());

  auto reply5 = rh->route(McGetRequest("0"));
  EXPECT_EQ("d", carbon::valueRangeSlow(reply5).str());
}

TEST(failoverRouteTest, leastFailuresCycle) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "d"))};

  mockFiberContext();
  folly::dynamic json =
      folly::dynamic::object("type", "LeastFailuresPolicy")("max_tries", 2);
  auto rh = makeFailoverRouteLeastFailures<McrouterRouterInfo, FailoverRoute>(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false,
      /* enableLeasePairing */ false,
      "route01",
      json);

  auto reply1 = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply1).str());

  auto reply2 = rh->route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply2).str());

  auto reply3 = rh->route(McGetRequest("0"));
  EXPECT_EQ("d", carbon::valueRangeSlow(reply3).str());

  auto reply4 = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply4).str());

  auto reply5 = rh->route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply5).str());

  auto reply6 = rh->route(McGetRequest("0"));
  EXPECT_EQ("d", carbon::valueRangeSlow(reply6).str());
}

TEST(failoverRouteTest, leastFailuresFailAll) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))};

  mockFiberContext();
  folly::dynamic json =
      folly::dynamic::object("type", "LeastFailuresPolicy")("max_tries", 3);
  auto rh = makeFailoverRouteLeastFailures<McrouterRouterInfo, FailoverRoute>(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false,
      /* enableLeasePairing */ false,
      "route01",
      json);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, leastFailuresFailAllLimit) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))};

  mockFiberContext();
  folly::dynamic json =
      folly::dynamic::object("type", "LeastFailuresPolicy")("max_tries", 2);
  auto rh = makeFailoverRouteLeastFailures<McrouterRouterInfo, FailoverRoute>(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false,
      /* enableLeasePairing */ false,
      "route01",
      json);

  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply).str());
}

TEST(failoverRouteTest, leastFailuresComplex) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_timeout, "a"),
          UpdateRouteTestData(mc_res_timeout),
          DeleteRouteTestData()),
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_timeout, "b"),
          UpdateRouteTestData(mc_res_stored),
          DeleteRouteTestData()),
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_timeout, "c"),
          UpdateRouteTestData(mc_res_timeout),
          DeleteRouteTestData())};

  mockFiberContext();
  folly::dynamic json =
      folly::dynamic::object("type", "LeastFailuresPolicy")("max_tries", 2);
  auto rh = makeFailoverRouteLeastFailures<McrouterRouterInfo, FailoverRoute>(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false,
      /* enableLeasePairing */ false,
      "route01",
      json);

  auto reply1 = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply1).str());

  auto reply2 = rh->route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply2).str());

  auto reply3 = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply3).str());

  // At this point, b has failed 2 times, c has failed 1 time
  // Next request is routed to c
  McSetRequest req("0");
  req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "value");
  rh->route(req);

  // Now both b and c have error count 2.  Next request routed to b.
  // b's error count will be reset to 0 on success
  // c still has error count 2
  rh->route(std::move(req));

  // Fail b twice
  auto reply4 = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply4).str());
  auto reply5 = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply5).str());

  // Now b and c have same error count (2)
  auto reply6 = rh->route(McGetRequest("0"));
  EXPECT_EQ("b", carbon::valueRangeSlow(reply6).str());

  auto reply7 = rh->route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply7).str());
}
