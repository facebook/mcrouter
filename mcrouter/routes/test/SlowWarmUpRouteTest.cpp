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
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/routes/SlowWarmUpRoute.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using TestHandle = TestHandleImpl<TestRouteHandleIf>;

namespace {

McrouterInstance* getRouter() {
  McrouterOptions opts = defaultTestOptions();
  opts.config_str = "{ \"route\": \"NullRoute\" }";
  return McrouterInstance::init("test_shadow", opts);
}

std::shared_ptr<ProxyRequestContext> getContext() {
  return ProxyRequestContext::createRecording(*getRouter()->getProxy(0),
                                              nullptr);
}

void sendWorkload(TestRouteHandle<SlowWarmUpRoute<TestRouteHandleIf>>& rh,
                  size_t numReqs, size_t& numNormal, size_t& numFailover) {
  for (size_t i = 0; i < numReqs; ++i) {
    auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
    auto val = toString(reply.value());
    if (val == "a") { // normal
      ++numNormal;
    } else if (val == "b") { // failover
      ++numFailover;
    }
  }
}

} // anonymous namespace

TEST(SlowWarmUpRoute, basic) {
  auto settings = std::make_shared<SlowWarmUpRouteSettings>(
      /* enable */    0.5,
      /* disable */   0.9,
      /* start */     0.1,
      /* step */      1.0,
      /* numReqs  */  5);

  TestFiberManager fm{fiber_local::ContextTypeTag()};

  std::vector<std::shared_ptr<TestHandle>> targets{
    std::make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a"),
                                 UpdateRouteTestData(mc_res_stored),
                                 DeleteRouteTestData(mc_res_notfound)),
    std::make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b"),
                                 UpdateRouteTestData(mc_res_stored),
                                 DeleteRouteTestData(mc_res_notfound)),
  };
  auto target = get_route_handles(targets)[0];
  auto failoverTarget = get_route_handles(targets)[1];

  auto ctx = getContext();
  fm.run([&]() {
    fiber_local::setSharedCtx(ctx);
    TestRouteHandle<SlowWarmUpRoute<TestRouteHandleIf>> rh(
        std::move(target),
        std::move(failoverTarget),
        std::move(settings));

    // send 10 gets -> moves hit rate to 1.
    for (int i = 0; i < 10; ++i) {
      auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
      EXPECT_EQ("a", toString(reply.value()));
    }

    // send 90 deletes (which return miss) -> moves hit rate to 0.1
    for (int i = 0; i < 90; ++i) {
      auto reply = rh.route(McRequest("0"), McOperation<mc_op_delete>());
      EXPECT_EQ(mc_res_notfound, reply.result());
    }

    // send 100 gets (round 1) -> should have normal and failover results
    size_t numNormal1 = 0;
    size_t numFailover1 = 0;
    sendWorkload(rh, 100, numNormal1, numFailover1);
    EXPECT_TRUE(numNormal1 > 0);
    EXPECT_TRUE(numFailover1 > 0);
    EXPECT_EQ(100, numNormal1 + numFailover1);

    // send a large amount of gets -> move hit rate up, but not above 0.9
    for (int i = 0; i < 500; ++i) {
      auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
    }

    // send 100 gets (round 2) -> should have more normal results than 1st round
    size_t numNormal2 = 0;
    size_t numFailover2 = 0;
    sendWorkload(rh, 100, numNormal2, numFailover2);
    EXPECT_TRUE(numNormal2 > 0);
    EXPECT_TRUE(numFailover2 > 0);
    EXPECT_EQ(100, numNormal2 + numFailover2);
    EXPECT_TRUE(numNormal2 > numNormal1);
    EXPECT_TRUE(numFailover2 < numFailover1);

    // send a large amount of gets -> move hit rate above 0.9
    for (int i = 0; i < 5000; ++i) {
      auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
    }

    // send 10 gets -> we should have moved out of the "warming up" state.
    for (int i = 0; i < 10; ++i) {
      auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
      EXPECT_EQ("a", toString(reply.value()));
    }
  });
}

TEST(SlowWarmUpRoute, minRequests) {
  auto settings = std::make_shared<SlowWarmUpRouteSettings>(
      /* enable */    0.5,
      /* disable */   0.9,
      /* start */     0.1,
      /* step */      1.0,
      /* numReqs  */  100);

  TestFiberManager fm{fiber_local::ContextTypeTag()};

  std::vector<std::shared_ptr<TestHandle>> targets{
    std::make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a"),
                                 UpdateRouteTestData(mc_res_stored),
                                 DeleteRouteTestData(mc_res_notfound)),
    std::make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b"),
                                 UpdateRouteTestData(mc_res_stored),
                                 DeleteRouteTestData(mc_res_notfound)),
  };
  auto target = get_route_handles(targets)[0];
  auto failoverTarget = get_route_handles(targets)[1];

  auto ctx = getContext();
  fm.run([&]() {
    fiber_local::setSharedCtx(ctx);
    TestRouteHandle<SlowWarmUpRoute<TestRouteHandleIf>> rh(
        std::move(target),
        std::move(failoverTarget),
        std::move(settings));

    // send 90 deletes (which return miss) -> hit rate is 0.
    for (int i = 0; i < 90; ++i) {
      auto reply = rh.route(McRequest("0"), McOperation<mc_op_delete>());
      EXPECT_EQ(mc_res_notfound, reply.result());
    }

    // as we have not reached minReqs yet (100), use always normal target.
    for (int i = 0; i < 10; ++i) {
      auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
      EXPECT_EQ("a", toString(reply.value()));
    }

    // now that we have achieved minReqs, receive some failovers.
    size_t numNormal = 0;
    size_t numFailover = 0;
    sendWorkload(rh, 50, numNormal, numFailover);
    EXPECT_TRUE(numFailover > 0);
    EXPECT_EQ(50, numNormal + numFailover);
  });
}
