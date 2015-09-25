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
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include <folly/dynamic.h>

#include "mcrouter/routes/DefaultShadowPolicy.h"
#include "mcrouter/routes/ShadowRoute.h"
#include "mcrouter/routes/ShadowRouteIf.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

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

  TestFiberManager fm{fiber_local::ContextTypeTag()};

  auto settings = ShadowSettings::create(
      folly::dynamic::object("index_range", folly::dynamic{ 0, 1 }),
      *getTestRouter());

  auto shadowRhs = get_route_handles(shadowHandles);
  McrouterShadowData shadowData{
    {std::move(shadowRhs[0]), settings},
    {std::move(shadowRhs[1]), settings},
  };

  McrouterRouteHandle<ShadowRoute<DefaultShadowPolicy>> rh(
    normalRh,
    std::move(shadowData),
    DefaultShadowPolicy());

  fm.run([&] () {
    mockFiberContext();
    auto reply = rh.route(McRequest("key"), McOperation<mc_op_get>());

    EXPECT_TRUE(reply.result() == mc_res_found);
    EXPECT_TRUE(toString(reply.value()) == "a");
  });

  EXPECT_TRUE(shadowHandles[0]->saw_keys.empty());
  EXPECT_TRUE(shadowHandles[1]->saw_keys.empty());
  settings->setKeyRange(0, 1);
  settings->setValidateReplies(true);

  fm.run([&] () {
    mockFiberContext();
    auto reply = rh.route(McRequest("key"), McOperation<mc_op_get>());

    EXPECT_TRUE(reply.result() == mc_res_found);
    EXPECT_TRUE(toString(reply.value()) == "a");
  });

  EXPECT_TRUE(shadowHandles[0]->saw_keys == vector<string>{"key"});
  EXPECT_TRUE(shadowHandles[1]->saw_keys == vector<string>{"key"});
}
