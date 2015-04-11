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

#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/ProxyMcReply.h"
#include "mcrouter/ProxyMcRequest.h"
#include "mcrouter/routes/FailoverWithExptimeRoute.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;
using std::vector;

using TestHandle = TestHandleImpl<McrouterRouteHandleIf>;

namespace {

std::shared_ptr<ProxyRequestContext> getContext() {
  McrouterOptions opts = defaultTestOptions();
  opts.config_str = "{ \"route\": \"NullRoute\" }";
  auto router = McrouterInstance::init("test_failover_with_exptime", opts);
  return ProxyRequestContext::createRecording(*router->getProxy(0), nullptr);
}

}  // anonymous namespace

TEST(failoverWithExptimeRouteTest, success) {
  vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  auto ctx = getContext();

  vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  McrouterRouteHandle<FailoverWithExptimeRoute> rh(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverWithExptimeSettings());

  auto reply = rh.route(ProxyMcRequest("0"), McOperation<mc_op_get>(), ctx);
  EXPECT_TRUE(toString(reply.value()) == "a");
  EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
}

TEST(failoverWithExptimeRouteTest, once) {
  vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  auto ctx = getContext();

  vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  McrouterRouteHandle<FailoverWithExptimeRoute> rh(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverWithExptimeSettings());

  auto reply = rh.route(ProxyMcRequest("0"), McOperation<mc_op_get>(), ctx);
  EXPECT_TRUE(toString(reply.value()) == "b");

  EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
  EXPECT_TRUE(failoverHandles[0]->sawExptimes == vector<uint32_t>{2});
}

TEST(failoverWithExptimeRouteTest, twice) {
  vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  auto ctx = getContext();

  vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  McrouterRouteHandle<FailoverWithExptimeRoute> rh(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverWithExptimeSettings());

  auto reply = rh.route(ProxyMcRequest("0"), McOperation<mc_op_get>(), ctx);
  EXPECT_TRUE(toString(reply.value()) == "c");

  EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
  EXPECT_TRUE(failoverHandles[0]->sawExptimes == vector<uint32_t>{2});
  EXPECT_TRUE(failoverHandles[1]->sawExptimes == vector<uint32_t>{2});
}

TEST(failoverWithExptimeRouteTest, fail) {
  vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  auto ctx = getContext();

  vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "c"))
  };

  McrouterRouteHandle<FailoverWithExptimeRoute> rh(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    FailoverWithExptimeSettings());

  auto reply = rh.route(ProxyMcRequest("0"), McOperation<mc_op_get>(), ctx);

  /* Will return the last reply when ran out of targets */
  EXPECT_TRUE(toString(reply.value()) == "c");

  EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
  EXPECT_TRUE(failoverHandles[0]->sawExptimes == vector<uint32_t>{2});
  EXPECT_TRUE(failoverHandles[1]->sawExptimes == vector<uint32_t>{2});
}

FailoverWithExptimeSettings::OperationSettings* getOpSettings(
    FailoverWithExptimeSettings& settings,
    mc_res_t res) {
  switch (res) {
    case mc_res_timeout:
      return &settings.dataTimeout;
    case mc_res_connect_timeout:
      return &settings.connectTimeout;
    case mc_res_tko:
      return &settings.tko;
    default:
      ADD_FAILURE() << "Unknown result: " << res;
      return nullptr;
  }
}

void testFailoverGet(mc_res_t res) {
  vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(res, "a")),
  };
  auto normalRh = get_route_handles(normalHandle);
  auto ctx = getContext();

  vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  FailoverWithExptimeSettings settings;
  auto opSettings = getOpSettings(settings, res);

  opSettings->gets = false;

  McrouterRouteHandle<FailoverWithExptimeRoute> rhNoFail(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    settings);

  auto reply = rhNoFail.route(ProxyMcRequest("0"),
                              McOperation<mc_op_get>(), ctx);
  EXPECT_EQ(toString(reply.value()), "a");
  EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});

  opSettings->gets = true;

  McrouterRouteHandle<FailoverWithExptimeRoute> rhFail(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    settings);

  reply = rhFail.route(ProxyMcRequest("0"), McOperation<mc_op_get>(), ctx);
  EXPECT_EQ(toString(reply.value()), "b");
}

void testFailoverUpdate(mc_res_t res) {
  vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(UpdateRouteTestData(res)),
  };
  auto normalRh = get_route_handles(normalHandle);
  auto ctx = getContext();

  vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))
  };

  FailoverWithExptimeSettings settings;
  auto opSettings = getOpSettings(settings, res);

  opSettings->updates = false;

  McrouterRouteHandle<FailoverWithExptimeRoute> rhNoFail(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    settings);

  auto msg = createMcMsgRef("0", "a");
  auto reply = rhNoFail.route(ProxyMcRequest(std::move(msg)),
                              McOperation<mc_op_set>(), ctx);
  EXPECT_EQ(toString(reply.value()), "a");
  EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
  // only normal handle sees the key
  EXPECT_TRUE(normalHandle[0]->saw_keys == vector<std::string>{"0"});
  EXPECT_EQ(failoverHandles[0]->saw_keys.size(), 0);
  EXPECT_EQ(failoverHandles[1]->saw_keys.size(), 0);

  opSettings->updates = true;

  McrouterRouteHandle<FailoverWithExptimeRoute> rhFail(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    settings);

  msg = createMcMsgRef("0", "a");
  reply = rhFail.route(ProxyMcRequest(std::move(msg)),
                       McOperation<mc_op_set>(), ctx);
  EXPECT_EQ(toString(reply.value()), "a");
  EXPECT_EQ(failoverHandles[0]->saw_keys.size(), 1);
  EXPECT_EQ(failoverHandles[1]->saw_keys.size(), 0);
}

void testFailoverDelete(mc_res_t res) {
  vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(DeleteRouteTestData(res)),
  };
  auto normalRh = get_route_handles(normalHandle);
  auto ctx = getContext();

  vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_deleted)),
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_deleted))
  };

  FailoverWithExptimeSettings settings;
  auto opSettings = getOpSettings(settings, res);

  opSettings->deletes = false;

  McrouterRouteHandle<FailoverWithExptimeRoute> rhNoFail(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    settings);

  auto msg = createMcMsgRef("0");
  auto reply = rhNoFail.route(ProxyMcRequest(std::move(msg)),
                              McOperation<mc_op_delete>(), ctx);
  EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
  // only normal handle sees the key
  EXPECT_TRUE(normalHandle[0]->saw_keys == vector<std::string>{"0"});
  EXPECT_EQ(failoverHandles[0]->saw_keys.size(), 0);
  EXPECT_EQ(failoverHandles[1]->saw_keys.size(), 0);

  opSettings->deletes = true;

  McrouterRouteHandle<FailoverWithExptimeRoute> rhFail(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    settings);

  msg = createMcMsgRef("0");
  reply = rhFail.route(ProxyMcRequest(std::move(msg)),
                       McOperation<mc_op_delete>(), ctx);
  EXPECT_EQ(failoverHandles[0]->saw_keys.size(), 1);
  EXPECT_EQ(failoverHandles[1]->saw_keys.size(), 0);
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
  vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_connect_timeout)),
  };
  auto normalRh = get_route_handles(normalHandle);
  auto ctx = getContext();

  vector<std::shared_ptr<TestHandle>> failoverHandles{
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))
  };

  FailoverWithExptimeSettings settings;

  McrouterRouteHandle<FailoverWithExptimeRoute> rh(
    normalRh[0],
    get_route_handles(failoverHandles),
    2,
    settings);

  auto msg = createMcMsgRef("0", "1");
  auto reply = rh.route(ProxyMcRequest(std::move(msg)),
                        McOperation<mc_op_incr>(), ctx);
  EXPECT_TRUE(normalHandle[0]->sawExptimes == vector<uint32_t>{0});
  // only normal handle sees the key
  EXPECT_TRUE(normalHandle[0]->saw_keys == vector<std::string>{"0"});
  EXPECT_TRUE(failoverHandles[0]->saw_keys.size() == 0);
  EXPECT_TRUE(failoverHandles[1]->saw_keys.size() == 0);
}
