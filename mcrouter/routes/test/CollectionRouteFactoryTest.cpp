/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <vector>

#include <gtest/gtest.h>

#include <folly/Range.h>
#include <folly/json.h>

#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/lib/carbon/example/gen/HelloGoodbyeRouterInfo.h"
#include "mcrouter/routes/CollectionRouteFactory.h"
#include "mcrouter/routes/test/RouteHandleTestBase.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

using namespace facebook::memcache::mcrouter;
using namespace hellogoodbye;

namespace facebook {
namespace memcache {
namespace mcrouter {

class AllSyncCollectionRouteFactoryTest
    : public RouteHandleTestBase<HelloGoodbyeRouterInfo> {
 public:
  HelloGoodbyeRouterInfo::RouteHandlePtr getAllSyncCollectionRoute(
      folly::StringPiece jsonStr) {
    return createAllSyncCollectionRoute<HelloGoodbyeRouterInfo>(
        rhFactory_, folly::parseJson(jsonStr));
  }

  void testCreate(folly::StringPiece config) {
    TestFiberManager fm;

    auto rh = getAllSyncCollectionRoute(config);
    ASSERT_TRUE(rh);
    EXPECT_EQ("AllSyncCollectionRoute", rh->routeName());

    fm.run([&rh]() {
      GoodbyeRequest req;
      auto reply = rh->route(req);
      EXPECT_TRUE(isErrorResult(reply.result()));
    });
  }
};

TEST_F(AllSyncCollectionRouteFactoryTest, create) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
      {
        "type": "AllSyncRoute",
        "children": [
          "NullRoute",
          "ErrorRoute"
        ]
      }
    )";

  testCreate(kSelectionRouteConfig);
}

} // end namespace mcrouter
} // end namespace memcache
} // end namespace facebook
