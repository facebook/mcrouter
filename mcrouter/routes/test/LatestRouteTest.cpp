/*
 *  Copyright (c) 2016, Facebook, Inc.
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

#include <folly/dynamic.h>
#include <folly/Hash.h>

#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeLatestRoute(
  const folly::dynamic& json,
  std::vector<McrouterRouteHandlePtr> targets,
  size_t id);

}}}  // facebook::memcache::mcrouter

TEST(latestRouteTest, one) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d")),
  };

  mockFiberContext();
  folly::dynamic settings = folly::dynamic::object("failover_count", 3);
  auto rh = makeLatestRoute(settings, get_route_handles(test_handles), 0);

  char first =
    folly::hash::hash_combine(0, globals::hostid()) % test_handles.size();

  /* While first is good, will keep sending to it */
  EXPECT_EQ(std::string(1, 'a' + first), replyFor(*rh, "key"));
  EXPECT_EQ(std::string(1, 'a' + first), replyFor(*rh, "key"));
  test_handles[first]->setTko();
  /* first is TKO, send to other one */
  auto second = replyFor(*rh, "key")[0] - 'a';
  EXPECT_NE(first, second);
  test_handles[first]->unsetTko();
  test_handles[second]->setTko();
  /* first is not TKO */
  EXPECT_EQ(std::string(1, 'a' + first), replyFor(*rh, "key"));
  test_handles[first]->setTko();
  /* first and second are now TKO */
  auto third = replyFor(*rh, "key")[0] - 'a';
  EXPECT_NE(first, third);
  EXPECT_NE(second, third);
  test_handles[third]->setTko();
  /* three boxes are now TKO, we hit the failover limit */
  auto reply = rh->route(McGetRequest("key"));
  EXPECT_EQ(mc_res_tko, reply.result());
}

TEST(latestRouteTest, thread_local_failover) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d")),
  };

  mockFiberContext();
  folly::dynamic settings = folly::dynamic::object;
  settings["failover_count"] = 3;
  settings["thread_local_failover"] = true;
  auto rh = makeLatestRoute(settings, get_route_handles(test_handles),
                            /* threadId */ 1);

  size_t curHash = folly::hash::hash_combine(0, globals::hostid());
  char first = folly::hash::hash_combine(curHash, 1) % test_handles.size();

  // Verify it respects threadId correctly
  EXPECT_EQ(std::string(1, 'a' + first), replyFor(*rh, "key"));

  // Disable thread_local_failover
  settings["thread_local_failover"] = false;
  auto rh2 = makeLatestRoute(settings, get_route_handles(test_handles),
                             /* threadId */ 1);
  char second = curHash % test_handles.size();
  EXPECT_EQ(std::string(1, 'a' + second), replyFor(*rh2, "key"));
}

TEST(latestRouteTest, leasePairingNoName) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d")),
  };

  mockFiberContext();
  folly::dynamic settings = folly::dynamic::object(
      "enable_lease_pairing", true)("failover_count", 3);

  EXPECT_ANY_THROW({
    auto rh = makeLatestRoute(settings, get_route_handles(test_handles), 0);
  });
}

TEST(latestRouteTest, leasePairingWithName) {
  std::vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "d")),
  };

  mockFiberContext();
  folly::dynamic settings = folly::dynamic::object(
      "enable_lease_pairing", true)("name", "01")("failover_count", 3);

  // Should not throw, as the name was provided
  auto rh = makeLatestRoute(settings, get_route_handles(test_handles), 0);
}
