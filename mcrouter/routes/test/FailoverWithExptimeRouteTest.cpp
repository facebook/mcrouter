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

#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/routes/FailoverRateLimiter.h"
#include "mcrouter/routes/FailoverWithExptimeRouteFactory.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;
using std::vector;

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace {
using FiberManagerContextTag =
    typename fiber_local<MemcacheRouterInfo>::ContextTypeTag;
} // anonymous
}
}
} // facebook::memcache::mcrouter

TEST(failoverWithExptimeRouteTest, success) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  auto rh = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(),
      nullptr);

  TestFiberManager fm{FiberManagerContextTag()};
  fm.run([&] {
    mockFiberContext();
    auto reply = rh->route(McGetRequest("0"));
    EXPECT_EQ("a", carbon::valueRangeSlow(reply).str());
    EXPECT_EQ(vector<uint32_t>{0}, normalHandle[0]->sawExptimes);
  });
}

TEST(failoverWithExptimeRouteTest, once) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_timeout, "a"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_timeout)),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_found, "b"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_notfound)),
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_found, "c"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_notfound))};

  auto rh = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(),
      nullptr);

  TestFiberManager fm{FiberManagerContextTag()};
  fm.run([&] {
    mockFiberContext();
    auto reply = rh->route(McGetRequest("0"));
    EXPECT_EQ("b", carbon::valueRangeSlow(reply).str());

    auto reply2 = rh->route(McDeleteRequest("1"));
    EXPECT_EQ(mc_res_notfound, reply2.result());
    EXPECT_EQ(vector<uint32_t>({0, 0}), normalHandle[0]->sawExptimes);
    EXPECT_EQ(vector<uint32_t>({0, 0}), failoverHandles[0]->sawExptimes);
    EXPECT_EQ(vector<uint32_t>{}, failoverHandles[1]->sawExptimes);
  });
}

TEST(failoverWithExptimeRouteTest, twice) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_timeout, "a"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_timeout)),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_timeout, "b"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_timeout)),
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_found, "c"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_notfound))};

  auto rh = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(),
      nullptr);

  TestFiberManager fm{FiberManagerContextTag()};
  fm.run([&] {
    mockFiberContext();
    auto reply = rh->route(McGetRequest("0"));
    EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());

    auto reply2 = rh->route(McDeleteRequest("1"));
    EXPECT_EQ(mc_res_notfound, reply2.result());
    EXPECT_EQ(vector<uint32_t>({0, 0}), normalHandle[0]->sawExptimes);
    EXPECT_EQ(vector<uint32_t>({0, 0}), failoverHandles[0]->sawExptimes);
    EXPECT_EQ(vector<uint32_t>({0, 0}), failoverHandles[1]->sawExptimes);
  });
}

TEST(failoverWithExptimeRouteTest, fail) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_timeout, "a"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_timeout)),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_timeout, "b"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_timeout)),
      make_shared<TestHandle>(
          GetRouteTestData(mc_res_timeout, "c"),
          UpdateRouteTestData(),
          DeleteRouteTestData(mc_res_timeout))};

  auto rh = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(),
      nullptr);

  TestFiberManager fm{FiberManagerContextTag()};
  fm.run([&] {
    mockFiberContext();
    auto reply = rh->route(McGetRequest("0"));

    /* Will return the last reply when ran out of targets */
    EXPECT_EQ("c", carbon::valueRangeSlow(reply).str());

    auto reply2 = rh->route(McDeleteRequest("1"));
    EXPECT_EQ(mc_res_timeout, reply2.result());
    EXPECT_EQ(vector<uint32_t>({0, 0}), normalHandle[0]->sawExptimes);
    EXPECT_EQ(vector<uint32_t>({0, 0}), failoverHandles[0]->sawExptimes);
    EXPECT_EQ(vector<uint32_t>({0, 0}), failoverHandles[1]->sawExptimes);
  });
}

void testFailoverGet(mc_res_t res) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
      make_shared<TestHandle>(GetRouteTestData(res, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
      make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))};

  auto rhNoFail = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(std::vector<std::string>{}),
      nullptr);

  TestFiberManager fm{FiberManagerContextTag()};
  fm.run([&] {
    mockFiberContext();
    auto reply = rhNoFail->route(McGetRequest("0"));
    EXPECT_EQ("a", carbon::valueRangeSlow(reply).str());
    EXPECT_EQ(vector<uint32_t>{0}, normalHandle[0]->sawExptimes);
  });

  auto rhFail = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(),
      nullptr);

  fm.run([&] {
    mockFiberContext();
    auto reply = rhFail->route(McGetRequest("0"));
    EXPECT_EQ("b", carbon::valueRangeSlow(reply).str());
  });
}

void testFailoverUpdate(mc_res_t res) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
      make_shared<TestHandle>(UpdateRouteTestData(res)),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored)),
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))};

  auto rhNoFail = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(std::vector<std::string>{}),
      nullptr);

  TestFiberManager fm{FiberManagerContextTag()};
  fm.run([&] {
    mockFiberContext();
    McSetRequest req("0");
    req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "a");
    auto reply = rhNoFail->route(std::move(req));
    EXPECT_EQ(res, reply.result());
    EXPECT_EQ(vector<uint32_t>{0}, normalHandle[0]->sawExptimes);
    // only normal handle sees the key
    EXPECT_EQ(vector<std::string>{"0"}, normalHandle[0]->saw_keys);
    EXPECT_EQ(0, failoverHandles[0]->saw_keys.size());
    EXPECT_EQ(0, failoverHandles[1]->saw_keys.size());
  });

  auto rhFail = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(),
      nullptr);

  fm.run([&] {
    mockFiberContext();
    McSetRequest req("0");
    req.value() = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "a");
    auto reply = rhFail->route(std::move(req));

    EXPECT_EQ(mc_res_stored, reply.result());
    EXPECT_EQ(1, failoverHandles[0]->saw_keys.size());
    EXPECT_EQ(0, failoverHandles[1]->saw_keys.size());
  });
}

void testFailoverDelete(mc_res_t res) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
      make_shared<TestHandle>(DeleteRouteTestData(res)),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
      make_shared<TestHandle>(DeleteRouteTestData(mc_res_deleted)),
      make_shared<TestHandle>(DeleteRouteTestData(mc_res_deleted))};

  auto rhNoFail = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(std::vector<std::string>{}),
      nullptr);

  TestFiberManager fm{FiberManagerContextTag()};
  fm.run([&] {
    mockFiberContext();
    McDeleteRequest req("0");
    auto reply = rhNoFail->route(McDeleteRequest("0"));
    EXPECT_EQ(vector<uint32_t>{0}, normalHandle[0]->sawExptimes);
    // only normal handle sees the key
    EXPECT_EQ(vector<std::string>{"0"}, normalHandle[0]->saw_keys);
    EXPECT_EQ(0, failoverHandles[0]->saw_keys.size());
    EXPECT_EQ(0, failoverHandles[1]->saw_keys.size());
  });

  auto rhFail = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(),
      nullptr);

  fm.run([&] {
    mockFiberContext();
    auto reply = rhFail->route(McDeleteRequest("0"));
    EXPECT_EQ(1, failoverHandles[0]->saw_keys.size());
    EXPECT_EQ(0, failoverHandles[1]->saw_keys.size());
  });
}

TEST(failoverWithExptimeRouteTest, noFailoverOnConnectTimeout) {
  testFailoverGet(mc_res_connect_timeout);
  testFailoverUpdate(mc_res_connect_timeout);
  testFailoverDelete(mc_res_connect_timeout);
}

TEST(failoverWithExptimeRouteTest, noFailoverOnDataTimeout) {
  testFailoverGet(mc_res_timeout);
  testFailoverUpdate(mc_res_timeout);
  testFailoverDelete(mc_res_timeout);
}

TEST(failoverWithExptimeRouteTest, noFailoverOnTko) {
  testFailoverGet(mc_res_tko);
  testFailoverUpdate(mc_res_tko);
  testFailoverDelete(mc_res_tko);
}

TEST(failoverWithExptimeRouteTest, noFailoverOnArithmetic) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_connect_timeout)),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored)),
      make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))};

  auto rh = createFailoverWithExptimeRoute<McrouterRouterInfo>(
      normalRh[0],
      get_route_handles(failoverHandles),
      2,
      FailoverErrorsSettings(),
      nullptr);

  TestFiberManager fm{FiberManagerContextTag()};
  fm.run([&] {
    mockFiberContext();
    McIncrRequest req("0");
    req.delta() = 1;
    auto reply = rh->route(std::move(req));

    EXPECT_EQ(vector<uint32_t>{0}, normalHandle[0]->sawExptimes);
    // only normal handle sees the key
    EXPECT_EQ(vector<std::string>{"0"}, normalHandle[0]->saw_keys);
    EXPECT_EQ(0, failoverHandles[0]->saw_keys.size());
    EXPECT_EQ(0, failoverHandles[1]->saw_keys.size());
  });
}
