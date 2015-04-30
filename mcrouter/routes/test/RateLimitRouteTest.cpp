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

#include <folly/json.h>

#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/RateLimitRoute.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;
using std::string;
using std::vector;

using TestHandle = TestHandleImpl<McrouterRouteHandleIf>;

/* These tests simply ensure that the various settings are read correctly.
   TokenBucket has its own tests */

namespace {

template <class Data, class Operation>
void test(Data data, Operation,
          mc_res_t ok, mc_res_t reject,
          const std::string& type, bool burst = false) {
  vector<std::shared_ptr<TestHandle>> normalHandle{
    make_shared<TestHandle>(std::move(data)),
  };
  auto normalRh = get_route_handles(normalHandle)[0];

  auto json = parseJsonString(
    (burst ?
     string("{\"") + type + "_rate\": 4.0, \"" + type + "_burst\": 3.0}" :
     string("{\"") + type + "_rate\": 2.0}"));

  McrouterRouteHandle<RateLimitRoute> rh(
    normalRh,
    RateLimiter(json));

  if (burst) {
    usleep(1001000);
    /* Rate is 4/sec, but we can only have 3 at a time */
    auto reply = rh.route(McRequest("key"), Operation());
    EXPECT_EQ(reply.result(), ok);
    reply = rh.route(McRequest("key"), Operation());
    EXPECT_EQ(reply.result(), ok);
    reply = rh.route(McRequest("key"), Operation());
    EXPECT_EQ(reply.result(), ok);
    reply = rh.route(McRequest("key"), Operation());
    EXPECT_EQ(reply.result(), reject);
  } else {
    usleep(501000);
    auto reply = rh.route(McRequest("key"), Operation());
    EXPECT_EQ(reply.result(), ok);
    reply = rh.route(McRequest("key"), Operation());
    EXPECT_EQ(reply.result(), reject);
  }
}

void testSets(bool burst = false) {
  test(UpdateRouteTestData(mc_res_stored), McOperation<mc_op_set>(),
       mc_res_stored, mc_res_notstored,
       "sets", burst);
}

void testGets(bool burst = false) {
  test(GetRouteTestData(mc_res_found, "a"), McOperation<mc_op_get>(),
       mc_res_found, mc_res_notfound,
       "gets", burst);
}

void testDeletes(bool burst = false) {
  test(DeleteRouteTestData(mc_res_deleted), McOperation<mc_op_delete>(),
       mc_res_deleted, mc_res_notfound,
       "deletes", burst);
}

}  // namespace

TEST(rateLimitRouteTest, setsBasic) { testSets(); }
TEST(rateLimitRouteTest, setsBurst) { testSets(true); }
TEST(rateLimitRouteTest, getsBasic) { testGets(); }
TEST(rateLimitRouteTest, getsBurst) { testGets(true); }
TEST(rateLimitRouteTest, deletesBasic) { testDeletes(); }
TEST(rateLimitRouteTest, deletesBurst) { testDeletes(true); }
