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

#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/routes/FailoverRateLimiter.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;
using std::vector;

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
    McrouterRouteHandlePtr normal,
    std::vector<McrouterRouteHandlePtr> failover,
    int32_t failoverExptime,
    FailoverErrorsSettings failoverErrors,
    std::unique_ptr<FailoverRateLimiter> rateLimiter);

}}}  // facebook::memcache::mcrouter

TEST(failoverWithExptimeRouteTest, success) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  auto rh = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(),
    nullptr);

  TestFiberManager fm{fiber_local::ContextTypeTag()};
  fm.run([&]{
    mockFiberContext();
    auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());
    EXPECT_TRUE(toString(reply.value()) == "a");
    EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
  });
}

TEST(failoverWithExptimeRouteTest, once) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  auto rh = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(),
    nullptr);

  TestFiberManager fm{fiber_local::ContextTypeTag()};
  fm.run([&]{
    mockFiberContext();
    auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());
    EXPECT_TRUE(toString(reply.value()) == "b");

    EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
    EXPECT_TRUE(failoverHandles[0]->sawExptimes == vector<uint32_t>{2});
  });
}

TEST(failoverWithExptimeRouteTest, twice) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  auto rh = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(),
    nullptr);

  TestFiberManager fm{fiber_local::ContextTypeTag()};
  fm.run([&]{
    mockFiberContext();
    auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());
    EXPECT_TRUE(toString(reply.value()) == "c");

    EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
    EXPECT_TRUE(failoverHandles[0]->sawExptimes == vector<uint32_t>{2});
    EXPECT_TRUE(failoverHandles[1]->sawExptimes == vector<uint32_t>{2});
  });
}

TEST(failoverWithExptimeRouteTest, fail) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))
  };

  auto rh = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(),
    nullptr);

  TestFiberManager fm{fiber_local::ContextTypeTag()};
  fm.run([&]{
    mockFiberContext();
    auto reply = rh->route(McRequest("0"), McOperation<mc_op_get>());

    /* Will return the last reply when ran out of targets */
    EXPECT_TRUE(toString(reply.value()) == "c");

    EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
    EXPECT_TRUE(failoverHandles[0]->sawExptimes == vector<uint32_t>{2});
    EXPECT_TRUE(failoverHandles[1]->sawExptimes == vector<uint32_t>{2});
  });
}

void testFailoverGet(mc_res_t res) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(res, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  auto rhNoFail = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(std::vector<std::string>{}),
    nullptr);


  TestFiberManager fm{fiber_local::ContextTypeTag()};
  fm.run([&]{
    mockFiberContext();
    auto reply = rhNoFail->route(McRequest("0"), McOperation<mc_op_get>());
    EXPECT_EQ(toString(reply.value()), "a");
    EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
  });

  auto rhFail = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(),
    nullptr);

  fm.run([&]{
    mockFiberContext();
    auto reply = rhFail->route(McRequest("0"), McOperation<mc_op_get>());
    EXPECT_EQ(toString(reply.value()), "b");
  });
}

void testFailoverUpdate(mc_res_t res) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(UpdateRouteTestData(res)),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))
  };

  auto rhNoFail = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(std::vector<std::string>{}),
    nullptr);

  TestFiberManager fm{fiber_local::ContextTypeTag()};
  fm.run([&]{
    mockFiberContext();
    auto msg = createMcMsgRef("0", "a");
    auto reply = rhNoFail->route(McRequest(std::move(msg)),
                                 McOperation<mc_op_set>());
    EXPECT_EQ(toString(reply.value()), "a");
    EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
    // only normal handle sees the key
    EXPECT_TRUE(normalHandle[0]->saw_keys == vector<std::string>{"0"});
    EXPECT_EQ(failoverHandles[0]->saw_keys.size(), 0);
    EXPECT_EQ(failoverHandles[1]->saw_keys.size(), 0);
  });

  auto rhFail = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(),
    nullptr);

  fm.run([&]{
    mockFiberContext();
    auto msg = createMcMsgRef("0", "a");
    auto reply = rhFail->route(McRequest(std::move(msg)),
                               McOperation<mc_op_set>());
    EXPECT_EQ(toString(reply.value()), "a");
    EXPECT_EQ(failoverHandles[0]->saw_keys.size(), 1);
    EXPECT_EQ(failoverHandles[1]->saw_keys.size(), 0);
  });
}

void testFailoverDelete(mc_res_t res) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(DeleteRouteTestData(res)),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_deleted)),
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_deleted))
  };

  auto rhNoFail = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(std::vector<std::string>{}),
    nullptr);

  TestFiberManager fm{fiber_local::ContextTypeTag()};
  fm.run([&]{
    mockFiberContext();
    auto msg = createMcMsgRef("0");
    auto reply = rhNoFail->route(McRequest(std::move(msg)),
                                 McOperation<mc_op_delete>());
    EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
    // only normal handle sees the key
    EXPECT_TRUE(normalHandle[0]->saw_keys == vector<std::string>{"0"});
    EXPECT_EQ(failoverHandles[0]->saw_keys.size(), 0);
    EXPECT_EQ(failoverHandles[1]->saw_keys.size(), 0);
  });

  auto rhFail = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(),
    nullptr);

  fm.run([&]{
    mockFiberContext();
    auto msg = createMcMsgRef("0");
    auto reply = rhFail->route(McRequest(std::move(msg)),
                               McOperation<mc_op_delete>());
    EXPECT_EQ(failoverHandles[0]->saw_keys.size(), 1);
    EXPECT_EQ(failoverHandles[1]->saw_keys.size(), 0);
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

TEST(failoverWithExptimeRouteTest, noFailoverOnArithmatic) {
  std::vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_connect_timeout)),
  };
  auto normalRh = get_route_handles(normalHandle);
  std::vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))
  };

  auto rh = makeFailoverWithExptimeRoute(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverErrorsSettings(),
    nullptr);

  TestFiberManager fm{fiber_local::ContextTypeTag()};
  fm.run([&]{
    mockFiberContext();
    auto msg = createMcMsgRef("0", "1");
    auto reply = rh->route(McRequest(std::move(msg)),
                           McOperation<mc_op_incr>());
    EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
    // only normal handle sees the key
    EXPECT_TRUE(normalHandle[0]->saw_keys == vector<std::string>{"0"});
    EXPECT_TRUE(failoverHandles[0]->saw_keys.size() == 0);
    EXPECT_TRUE(failoverHandles[1]->saw_keys.size() == 0);
  });
}
