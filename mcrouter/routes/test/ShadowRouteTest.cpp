#include <memory>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "mcrouter/routes/DefaultShadowPolicy.h"
#include "mcrouter/routes/ShadowRoute.h"
#include "mcrouter/routes/ShadowRouteIf.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache::mcrouter;
using namespace facebook::memcache;

using std::make_shared;
using std::string;
using std::vector;

TEST(shadowRouteTest, defaultPolicy) {
  vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
  };
  auto normalRh = get_route_handles(normalHandle)[0];

  vector<std::shared_ptr<TestHandle>> shadowHandles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
  };

  TestFiberManager fm;

  auto data = make_shared<proxy_pool_shadowing_policy_t::Data>();
  vector<std::shared_ptr<proxy_pool_shadowing_policy_t>> settings {
    make_shared<proxy_pool_shadowing_policy_t>(data, nullptr),
    make_shared<proxy_pool_shadowing_policy_t>(data, nullptr),
  };

  auto shadowRhs = get_route_handles(shadowHandles);
  ShadowData<TestRouteHandleIf> shadowData = {
    {std::move(shadowRhs[0]), std::move(settings[0])},
    {std::move(shadowRhs[1]), std::move(settings[1])},
  };

  TestRouteHandle<ShadowRoute<TestRouteHandleIf, DefaultShadowPolicy>> rh(
    normalRh,
    std::move(shadowData),
    0,
    DefaultShadowPolicy());

  fm.runAll(
    {
      [&] () {
        auto reply = rh.route(McRequest("key"),
                              McOperation<mc_op_get>());

        EXPECT_TRUE(reply.result() == mc_res_found);
        EXPECT_TRUE(toString(reply.value()) == "a");
      }
    });

  EXPECT_TRUE(shadowHandles[0]->saw_keys.empty());
  EXPECT_TRUE(shadowHandles[1]->saw_keys.empty());
  data->end_index = 1;
  data->end_key_fraction = 1.0;

  fm.runAll(
    {
      [&] () {
        auto reply = rh.route(McRequest("key"),
                              McOperation<mc_op_get>());

        EXPECT_TRUE(reply.result() == mc_res_found);
        EXPECT_TRUE(toString(reply.value()) == "a");
      }
    });

  EXPECT_TRUE(shadowHandles[0]->saw_keys == vector<string>{"key"});
  EXPECT_TRUE(shadowHandles[1]->saw_keys == vector<string>{"key"});
}
