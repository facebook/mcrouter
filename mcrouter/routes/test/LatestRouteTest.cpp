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

#include <folly/dynamic.h>
#include <folly/Hash.h>

#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeLatestRoute(
  const folly::dynamic& json,
  std::vector<McrouterRouteHandlePtr> targets);

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
  auto rh = makeLatestRoute(settings, get_route_handles(test_handles));

  char first =
    folly::hash::hash_combine(0, globals::hostid()) % test_handles.size();

  /* While first is good, will keep sending to it */
  EXPECT_EQ(replyFor(*rh, "key"), std::string(1, 'a' + first));
  EXPECT_EQ(replyFor(*rh, "key"), std::string(1, 'a' + first));
  test_handles[first]->setTko();
  /* first is TKO, send to other one */
  auto second = replyFor(*rh, "key")[0] - 'a';
  EXPECT_NE(first, second);
  test_handles[first]->unsetTko();
  test_handles[second]->setTko();
  /* first is not TKO */
  EXPECT_EQ(replyFor(*rh, "key"), std::string(1, 'a' + first));
  test_handles[first]->setTko();
  /* first and second are now TKO */
  auto third = replyFor(*rh, "key")[0] - 'a';
  EXPECT_NE(first, third);
  EXPECT_NE(second, third);
  test_handles[third]->setTko();
  /* three boxes are now TKO, we hit the failover limit */
  auto reply = rh->route(McRequest("key"), McOperation<mc_op_get>());
  EXPECT_EQ(reply.result(), mc_res_tko);
}
