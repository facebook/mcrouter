#include <gtest/gtest.h>
#include <glog/logging.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/config/test/TestRouteHandleProvider.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;

TEST(RouteHandleFactoryTest, sanity) {
  TestFiberManager fm;

  RouteHandleProvider<TestRouteHandleIf> provider;

  RouteHandleFactory<TestRouteHandleIf> factory(provider);

  auto rh = factory.create("AllAsyncRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_EQ(reply.result(), mc_res_notfound);
  });

  rh = factory.create("AllFastestRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("AllInitialRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("AllMajorityRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("AllSyncRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("FailoverRoute|NullRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_EQ(reply.result(), mc_res_notfound);
  });

  rh = factory.create("HashRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("HostIdRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_TRUE(reply.isError());
  });

  rh = factory.create("LatestRoute|NullRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_EQ(reply.result(), mc_res_notfound);
  });

  rh = factory.create("MissFailoverRoute|NullRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McRequest("a"), McOperation<mc_op_get>());
    EXPECT_EQ(reply.result(), mc_res_notfound);
  });
}
