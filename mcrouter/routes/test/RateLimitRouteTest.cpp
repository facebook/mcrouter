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
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include <folly/json.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/network/gen/Memcache.h"
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

template <class Data, class Request>
void test(
    Data data,
    Request,
    mc_res_t ok,
    mc_res_t reject,
    const std::string& type,
    bool burst = false) {
  vector<std::shared_ptr<TestHandle>> normalHandle{
      make_shared<TestHandle>(std::move(data)),
  };
  auto normalRh = get_route_handles(normalHandle)[0];

  auto json = parseJsonString(
      (burst
           ? string("{\"") + type + "_rate\": 4.0, \"" + type + "_burst\": 3.0}"
           : string("{\"") + type + "_rate\": 2.0}"));

  McrouterRouteHandle<RateLimitRoute<McrouterRouteHandleIf>> rh(
      normalRh, RateLimiter(json));

  Request req("key");
  // McSetRequest requires value be set; this is a no-op for Get and Delete
  if (auto* value = const_cast<folly::IOBuf*>(carbon::valuePtrUnsafe(req))) {
    *value = folly::IOBuf(folly::IOBuf::COPY_BUFFER, "value");
  }

  if (burst) {
    usleep(1001000);
    /* Rate is 4/sec, but we can only have 3 at a time */
    auto reply = rh.route(req);
    EXPECT_EQ(reply.result(), ok);
    reply = rh.route(req);
    EXPECT_EQ(reply.result(), ok);
    reply = rh.route(req);
    EXPECT_EQ(reply.result(), ok);
    reply = rh.route(req);
    EXPECT_EQ(reply.result(), reject);
  } else {
    usleep(501000);
    auto reply = rh.route(req);
    EXPECT_EQ(reply.result(), ok);
    reply = rh.route(req);
    EXPECT_EQ(reply.result(), reject);
  }
}

void testSets(bool burst = false) {
  test(
      UpdateRouteTestData(mc_res_stored),
      McSetRequest(),
      mc_res_stored,
      mc_res_notstored,
      "sets",
      burst);
}

void testGets(bool burst = false) {
  test(
      GetRouteTestData(mc_res_found, "a"),
      McGetRequest(),
      mc_res_found,
      mc_res_notfound,
      "gets",
      burst);
}

void testDeletes(bool burst = false) {
  test(
      DeleteRouteTestData(mc_res_deleted),
      McDeleteRequest(),
      mc_res_deleted,
      mc_res_notfound,
      "deletes",
      burst);
}

} // anonymous

TEST(rateLimitRouteTest, setsBasic) {
  testSets();
}
TEST(rateLimitRouteTest, setsBurst) {
  testSets(true);
}
TEST(rateLimitRouteTest, getsBasic) {
  testGets();
}
TEST(rateLimitRouteTest, getsBurst) {
  testGets(true);
}
TEST(rateLimitRouteTest, deletesBasic) {
  testDeletes();
}
TEST(rateLimitRouteTest, deletesBurst) {
  testDeletes(true);
}
