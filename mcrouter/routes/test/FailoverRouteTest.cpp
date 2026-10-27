/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <folly/json/dynamic.h>

#include "mcrouter/lib/network/gen/MemcacheMessages.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/lib/test/TestRouteHandle.h"
#include "mcrouter/routes/FailoverRateLimiter.h"
#include "mcrouter/routes/FailoverRoute.h"
#include "mcrouter/routes/HashRouteFactory.h"
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
    std::string name = "",
    folly::dynamic json = folly::dynamic::object) {
  return makeFailoverRouteInOrder<McrouterRouterInfo, FailoverRoute>(
      std::move(rh),
      std::move(failoverErrors),
      std::move(rateLimiter),
      failoverTagging,
      enableLeasePairing,
      std::move(name),
      json);
}
} // namespace mcrouter
} // namespace memcache
} // namespace facebook

TEST(failoverRouteTest, nofailover) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "a"))};

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
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

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
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

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
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

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
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "c"))};

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
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::REMOTE_ERROR, "a")),
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::LOCAL_ERROR, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

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
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::REMOTE_ERROR, "a")),
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::LOCAL_ERROR, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

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
      make_shared<TestHandle>(
          UpdateRouteTestData(carbon::Result::REMOTE_ERROR)),
      make_shared<TestHandle>(UpdateRouteTestData(carbon::Result::LOCAL_ERROR)),
      make_shared<TestHandle>(UpdateRouteTestData(carbon::Result::FOUND))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(std::vector<std::string>{"remote_error"}),
      nullptr,
      /* failoverTagging */ false);

  McSetRequest req("0");
  req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "value");
  auto reply = rh->route(std::move(req));
  EXPECT_EQ(carbon::Result::LOCAL_ERROR, *reply.result());
}

TEST(failoverRouteTest, separateErrorsGet) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::REMOTE_ERROR, "a")),
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::LOCAL_ERROR, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

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
      make_shared<TestHandle>(
          UpdateRouteTestData(carbon::Result::REMOTE_ERROR)),
      make_shared<TestHandle>(UpdateRouteTestData(carbon::Result::LOCAL_ERROR)),
      make_shared<TestHandle>(UpdateRouteTestData(carbon::Result::STORED))};

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
    EXPECT_EQ(carbon::Result::STORED, *reply1.result());
  }
  {
    McAppendRequest req("0");
    req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "value");
    auto reply2 = rh->route(std::move(req));
    EXPECT_EQ(carbon::Result::STORED, *reply2.result());
  }
  {
    McPrependRequest req("0");
    req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "value");
    auto reply3 = rh->route(std::move(req));
    EXPECT_EQ(carbon::Result::STORED, *reply3.result());
  }
}

TEST(failoverRouteTest, separateErrorsDelete) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(DeleteRouteTestData(carbon::Result::LOCAL_ERROR)),
      make_shared<TestHandle>(
          DeleteRouteTestData(carbon::Result::REMOTE_ERROR)),
      make_shared<TestHandle>(DeleteRouteTestData(carbon::Result::DELETED))};

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
  EXPECT_EQ(carbon::Result::REMOTE_ERROR, *reply.result());
}

TEST(failoverRouteTest, rateLimit) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "b"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      std::make_unique<FailoverRateLimiter>(0.5, 1),
      /* failoverTagging */ false);

  // tokens: 1
  auto reply1 = rh->route(McGetRequest("0"));
  EXPECT_EQ(carbon::Result::FOUND, *reply1.result());
  // tokens: 0
  auto reply2 = rh->route(McGetRequest("0"));
  EXPECT_EQ(carbon::Result::TIMEOUT, *reply2.result());
  // tokens: 0.5
  auto reply3 = rh->route(McGetRequest("0"));
  EXPECT_EQ(carbon::Result::FOUND, *reply3.result());
  // tokens: 0
  auto reply4 = rh->route(McGetRequest("0"));
  EXPECT_EQ(carbon::Result::TIMEOUT, *reply4.result());
}

TEST(failoverRouteTest, rateLimitWithTKO) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

  mockFiberContext();
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      std::make_unique<FailoverRateLimiter>(0.5, 1),
      /* failoverTagging */ false);

  // tokens: 1
  auto reply1 = rh->route(McGetRequest("0"));
  EXPECT_EQ(carbon::Result::FOUND, *reply1.result());
  // tokens: 0
  auto reply2 = rh->route(McGetRequest("0"));
  EXPECT_EQ(carbon::Result::FOUND, *reply2.result());
  // tokens: 0.5
  auto reply3 = rh->route(McGetRequest("0"));
  EXPECT_EQ(carbon::Result::FOUND, *reply3.result());
  // tokens: 0
  auto reply4 = rh->route(McGetRequest("0"));
  EXPECT_EQ(carbon::Result::FOUND, *reply4.result());
}

TEST(failoverRouteTest, leastFailuresNoFailover) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

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
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

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
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

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
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "c")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "d"))};

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
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "c")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "d"))};

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
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "c"))};

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
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "c"))};

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
          GetRouteTestData(carbon::Result::TIMEOUT, "a"),
          UpdateRouteTestData(carbon::Result::TIMEOUT),
          DeleteRouteTestData()),
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::TIMEOUT, "b"),
          UpdateRouteTestData(carbon::Result::STORED),
          DeleteRouteTestData()),
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::TIMEOUT, "c"),
          UpdateRouteTestData(carbon::Result::TIMEOUT),
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

TEST(failoverRouteTest, deterministicOrderTimeout) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "c")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "d"))};

  mockFiberContext();
  folly::dynamic policyJson = folly::dynamic::object(
      "type", "DeterministicOrderPolicy")("max_tries", 4)("max_error_tries", 2);
  folly::dynamic json = folly::dynamic::object("failover_policy", policyJson);
  auto rh = makeFailoverRouteDefault<McrouterRouterInfo, FailoverRoute>(
      json, get_route_handles(test_handles));

  rh->route(McGetRequest("0"));
  // The request should reach 2 children
  int numRetries = 0;
  for (auto testHdl : test_handles) {
    if (!testHdl->saw_keys.empty()) {
      ++numRetries;
    }
  }
  // Limited by max_error_tries to 2.
  EXPECT_EQ(2, numRetries);
}

TEST(failoverRouteTest, deterministicOrderTko) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "c")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "d")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "e"))};

  mockFiberContext();
  folly::dynamic policyJson = folly::dynamic::object(
      "type", "DeterministicOrderPolicy")("max_tries", 4)("max_error_tries", 2);
  folly::dynamic json = folly::dynamic::object("failover_policy", policyJson);
  auto rh = makeFailoverRouteDefault<McrouterRouterInfo, FailoverRoute>(
      json, get_route_handles(test_handles));

  rh->route(McGetRequest("0"));
  // The request should reach 2 children
  int numRetries = 0;
  for (auto testHdl : test_handles) {
    if (!testHdl->saw_keys.empty()) {
      ++numRetries;
    }
  }
  // Since TKOs are excluded from max_error_tries, we'll end up trying 4 of the
  // 5 retry destinations.
  EXPECT_EQ(4, numRetries);
}

TEST(failoverRouteTest, deterministicOrderNoDoubleShot) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "c"))};

  mockFiberContext();
  auto failover_children = get_route_handles(test_handles);
  auto nServers = test_handles.size();
  failover_children.insert(
      failover_children.begin(),
      createHashRoute<McrouterRouterInfo, Ch3HashFunc>(
          get_route_handles(test_handles),
          "", /* no salt */
          Ch3HashFunc(nServers)));
  folly::dynamic policyJson =
      folly::dynamic::object("type", "DeterministicOrderPolicy")(
          "max_tries", nServers)("max_error_tries", nServers);
  folly::dynamic json = folly::dynamic::object("failover_policy", policyJson);
  auto rh = makeFailoverRouteDefault<McrouterRouterInfo, FailoverRoute>(
      json, failover_children);
  rh->route(McGetRequest("noDoubleShot"));
  for (auto testHdl : test_handles) {
    if (!testHdl->saw_keys.empty()) {
      --nServers;
    }
  }
  // If all test_handles are reached, no test_handle is hit twice
  EXPECT_EQ(0, nServers);
}

TEST(failoverRouteTest, ignoreTkoAndHardTko) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "a")),
      // hard TKO
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::CONNECT_ERROR, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "c")),
      // hard TKO
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::CONNECT_ERROR, "d"))};
  mockFiberContext();
  folly::dynamic policyJson = folly::dynamic::object(
      "type", "DeterministicOrderPolicy")("max_tries", 3)("max_error_tries", 2);
  folly::dynamic json = folly::dynamic::object("failover_policy", policyJson);
  auto rh = makeFailoverRouteDefault<McrouterRouterInfo, FailoverRoute>(
      json, get_route_handles(test_handles));
  rh->route(McGetRequest("0"));
  // The request should reach 2 children
  int numRetries = 0;
  for (auto testHdl : test_handles) {
    if (!testHdl->saw_keys.empty()) {
      ++numRetries;
    }
  }
  // Since TKOs and hard TKOs are excluded from max_error_tries, we'll end up
  // trying 3 of the 4 retry destinations, bounded by max_tries = 3
  EXPECT_EQ(3, numRetries);
}

TEST(failoverRouteTest, ignoreTkoHardTkoAndTryAgain) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::RES_TRY_AGAIN, "a")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TKO, "c")),
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::CONNECT_ERROR, "d")),
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::RES_TRY_AGAIN, "e"))};

  mockFiberContext();
  folly::dynamic excludeErrors = folly::dynamic::array;
  excludeErrors.push_back("try_again");
  folly::dynamic policyJson = folly::dynamic::object(
      "type", "DeterministicOrderPolicy")("max_tries", 5)("max_error_tries", 2)(
      "exclude_errors", excludeErrors);
  folly::dynamic json = folly::dynamic::object("failover_policy", policyJson);
  auto rh = makeFailoverRouteDefault<McrouterRouterInfo, FailoverRoute>(
      json, get_route_handles(test_handles));

  rh->route(McGetRequest("0"));
  // The request should reach 2 children
  int numRetries = 0;
  for (auto testHdl : test_handles) {
    if (!testHdl->saw_keys.empty()) {
      ++numRetries;
    }
  }
  // Since TKOs, CONNECT_ERROR and RES_TRY_AGAINs are excluded from
  // max_error_tries, we'll end up trying all 5 destinations.
  EXPECT_EQ(5, numRetries);
}

TEST(failoverRouteTest, limitRetries) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::TIMEOUT, "a")),
      make_shared<TestHandle>(
          GetRouteTestData(carbon::Result::RES_TRY_AGAIN, "b")),
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "c"))};

  mockFiberContext();
  folly::dynamic excludeErrors = folly::dynamic::array;
  excludeErrors.push_back("try_again");
  folly::dynamic json = folly::dynamic::object("type", "FailoverInOrderPolicy")(
      "max_tries", 2)("exclude_errors", excludeErrors);
  auto rh = makeFailoverRouteInOrder(
      get_route_handles(test_handles),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false,
      /* enableLeasePairing */ false,
      "route01",
      std::move(json));

  // Ordinarily, this test would fail since "b" will be the second and last
  // retry. However since we exclude try_again from being a retry, "c" is
  // retried.
  auto reply = rh->route(McGetRequest("0"));
  EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());
}

namespace {

// Replies like a server would: with a destination and, optionally, a server
// block report.
template <class RouteHandleIf>
struct ReportingRoute {
  static std::string routeName() {
    return "reporting";
  }

  ReportingRoute(
      carbon::Result result,
      const std::string& host,
      std::string report,
      std::string message)
      : result_(result),
        destination_(std::make_shared<const AccessPoint>(host, 11211)),
        report_(std::move(report)),
        message_(std::move(message)) {}

  template <class Request>
  bool traverse(const Request&, const RouteHandleTraverser<RouteHandleIf>&)
      const {
    return false;
  }

  template <class Request>
  ReplyT<Request> route(const Request&) const {
    ReplyT<Request> reply(result_);
    reply.setDestination(destination_);
    reply.setArtilleryServerBlockReport(report_);
    carbon::setMessageIfPresent(reply, message_);
    return reply;
  }

 private:
  carbon::Result result_;
  std::shared_ptr<const AccessPoint> destination_;
  std::string report_;
  std::string message_;
};

McrouterRouteHandlePtr makeReportingRoute(
    carbon::Result result,
    const std::string& host,
    std::string report = "",
    std::string message = "") {
  return makeRouteHandle<McrouterRouteHandleIf, ReportingRoute>(
      result, host, std::move(report), std::move(message));
}

McGetRequest tracedGet() {
  McGetRequest req("0");
  req.setTraceContext("AAAAAAAAAAABBBBBBBBBBB");
  return req;
}

McrouterRouteHandlePtr makeInOrder(std::vector<McrouterRouteHandlePtr> rhs) {
  return makeFailoverRouteInOrder(
      std::move(rhs),
      FailoverErrorsSettings(),
      nullptr,
      /* failoverTagging */ false);
}

} // namespace

TEST(failoverRouteTest, untracedRecordsNoAttempts) {
  mockFiberContext();
  auto rh = makeInOrder({
      makeReportingRoute(carbon::Result::BUSY, "a", "report-a"),
      makeReportingRoute(carbon::Result::FOUND, "b"),
  });

  auto reply = rh->route(McGetRequest("0"));

  EXPECT_EQ(carbon::Result::FOUND, *reply.result());
  EXPECT_TRUE(reply.failoverAttempts().empty());
}

TEST(failoverRouteTest, noFailoverRecordsNoAttempts) {
  mockFiberContext();
  auto rh = makeInOrder({
      makeReportingRoute(carbon::Result::FOUND, "a", "report-a"),
      makeReportingRoute(carbon::Result::FOUND, "b"),
  });

  auto reply = rh->route(tracedGet());

  EXPECT_EQ("report-a", reply.artilleryServerBlockReport());
  EXPECT_TRUE(reply.failoverAttempts().empty());
}

TEST(failoverRouteTest, tracedRecordsDroppedAttemptsInOrder) {
  std::vector<std::shared_ptr<TestHandle>> tko{
      make_shared<TestHandle>(GetRouteTestData(carbon::Result::FOUND, "x"))};
  tko[0]->setTko();
  mockFiberContext();
  auto rh = makeInOrder({
      makeReportingRoute(
          carbon::Result::BUSY, "a", "report-a", "server overloaded"),
      get_route_handles(tko)[0],
      makeReportingRoute(carbon::Result::FOUND, "c", "report-c"),
  });

  auto reply = rh->route(tracedGet());

  EXPECT_EQ(carbon::Result::FOUND, *reply.result());
  EXPECT_EQ("report-c", reply.artilleryServerBlockReport());
  const auto& attempts = reply.failoverAttempts();
  ASSERT_EQ(2, attempts.size());
  EXPECT_EQ(carbon::Result::BUSY, attempts[0].result);
  EXPECT_EQ("a:11211", attempts[0].destination);
  EXPECT_EQ("report-a", attempts[0].artilleryServerBlockReport);
  EXPECT_EQ("server overloaded", attempts[0].message);
  EXPECT_EQ(carbon::Result::TKO, attempts[1].result);
  EXPECT_TRUE(attempts[1].artilleryServerBlockReport.empty());
  EXPECT_TRUE(attempts[1].message.empty());
  EXPECT_LE(attempts[0].startTimeUs, attempts[0].endTimeUs);
  EXPECT_LE(attempts[0].endTimeUs, attempts[1].startTimeUs);
  EXPECT_LE(attempts[1].startTimeUs, attempts[1].endTimeUs);
}

// When every destination fails, the last reply is returned rather than
// dropped, so it is not an attempt.
TEST(failoverRouteTest, allFailedReturnsLastReplyNotAsAttempt) {
  mockFiberContext();
  auto rh = makeInOrder({
      makeReportingRoute(carbon::Result::BUSY, "a", "report-a"),
      makeReportingRoute(carbon::Result::BUSY, "b", "report-b"),
  });

  auto reply = rh->route(tracedGet());

  EXPECT_EQ(carbon::Result::BUSY, *reply.result());
  EXPECT_EQ("report-b", reply.artilleryServerBlockReport());
  ASSERT_EQ(1, reply.failoverAttempts().size());
  EXPECT_EQ("report-a", reply.failoverAttempts()[0].artilleryServerBlockReport);
}

namespace {

void expectNonOverlappingInOrder(
    const std::vector<carbon::FailoverAttempt>& attempts) {
  for (size_t i = 0; i < attempts.size(); ++i) {
    EXPECT_LE(attempts[i].startTimeUs, attempts[i].endTimeUs);
    if (i + 1 < attempts.size()) {
      EXPECT_LE(attempts[i].endTimeUs, attempts[i + 1].startTimeUs);
    }
  }
}

} // namespace

TEST(failoverRouteTest, nestedAttemptsSurviveWithoutOuterFailover) {
  mockFiberContext();
  auto inner = makeInOrder({
      makeReportingRoute(
          carbon::Result::BUSY, "a1", "report-a1", "inner server overloaded"),
      makeReportingRoute(carbon::Result::FOUND, "a2", "report-a2"),
  });
  auto fallback = make_shared<TestHandle>(
      GetRouteTestData(carbon::Result::FOUND, "fallback"));
  auto rh = makeInOrder({inner, fallback->rh});

  auto reply = rh->route(tracedGet());

  EXPECT_EQ(carbon::Result::FOUND, *reply.result());
  ASSERT_NE(nullptr, reply.destination());
  EXPECT_EQ("a2:11211", reply.destination()->toHostPortString());
  EXPECT_EQ("report-a2", reply.artilleryServerBlockReport());
  EXPECT_TRUE(fallback->saw_keys.empty());
  const auto& attempts = reply.failoverAttempts();
  ASSERT_EQ(1, attempts.size());
  EXPECT_EQ(carbon::Result::BUSY, attempts[0].result);
  EXPECT_EQ("a1:11211", attempts[0].destination);
  EXPECT_EQ("report-a1", attempts[0].artilleryServerBlockReport);
  EXPECT_EQ("inner server overloaded", attempts[0].message);
  EXPECT_GT(attempts[0].startTimeUs, 0);
  expectNonOverlappingInOrder(attempts);
}

TEST(failoverRouteTest, nestedFailoverAttemptsKeepOrder) {
  mockFiberContext();
  auto inner = makeInOrder({
      makeReportingRoute(carbon::Result::BUSY, "a1", "report-a1"),
      makeReportingRoute(carbon::Result::BUSY, "a2", "report-a2"),
  });
  auto rh = makeInOrder({
      inner,
      makeReportingRoute(carbon::Result::FOUND, "b", "report-b"),
  });

  auto reply = rh->route(tracedGet());

  EXPECT_EQ("report-b", reply.artilleryServerBlockReport());
  const auto& attempts = reply.failoverAttempts();
  ASSERT_EQ(2, attempts.size());
  EXPECT_EQ("report-a1", attempts[0].artilleryServerBlockReport);
  EXPECT_EQ("report-a2", attempts[1].artilleryServerBlockReport);
  // a2 starts where a1 ended, not when the inner route was entered.
  expectNonOverlappingInOrder(attempts);
}

// The returned child is itself a failover that dropped an attempt and returned
// a reply from a failover beneath it: all three levels' drops stay in order.
TEST(failoverRouteTest, attemptsUnderTheReturnedChildComeAfterOurs) {
  mockFiberContext();
  auto deep = makeInOrder({
      makeReportingRoute(carbon::Result::BUSY, "c1", "report-c1"),
      makeReportingRoute(carbon::Result::FOUND, "c2", "report-c2"),
  });
  auto middle = makeInOrder({
      makeReportingRoute(carbon::Result::BUSY, "b1", "report-b1"),
      deep,
  });
  auto rh = makeInOrder({
      makeReportingRoute(carbon::Result::BUSY, "a1", "report-a1"),
      middle,
  });

  auto reply = rh->route(tracedGet());

  EXPECT_EQ("report-c2", reply.artilleryServerBlockReport());
  const auto& attempts = reply.failoverAttempts();
  ASSERT_EQ(3, attempts.size());
  EXPECT_EQ("report-a1", attempts[0].artilleryServerBlockReport);
  EXPECT_EQ("report-b1", attempts[1].artilleryServerBlockReport);
  EXPECT_EQ("report-c1", attempts[2].artilleryServerBlockReport);
  expectNonOverlappingInOrder(attempts);
}
