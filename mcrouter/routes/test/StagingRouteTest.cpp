/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include <algorithm>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "mcrouter/lib/carbon/example/gen/HelloGoodbyeRouterInfo.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/lib/test/TestRouteHandle.h"

#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/StagingRoute.h"
#include "mcrouter/routes/test/RouteHandleTestBase.h"

using namespace hellogoodbye;
using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;
using std::vector;

namespace facebook {
namespace memcache {
namespace mcrouter {

using TestHandle = TestHandleImpl<MemcacheRouteHandleIf>;
using RouteHandle = MemcacheRouteHandle<StagingRoute>;

class StagingRouteTest : public RouteHandleTestBase<HelloGoodbyeRouterInfo> {
 public:
  template <class Request, class TestData>
  void createAndRun(
      const Request& req,
      TestData warmResult,
      TestData stagingResult,
      mc_res_t expectedReply) {
    warm_ = std::make_shared<TestHandle>(warmResult);
    staging_ = std::make_shared<TestHandle>(stagingResult);

    rh_ = std::make_shared<RouteHandle>(RouteHandle(warm_->rh, staging_->rh));

    TestFiberManager fm;
    fm.runAll({[&]() {
      auto reply = rh_->route(req);

      // we should always expect the warm reply
      EXPECT_EQ(reply.result(), expectedReply);
    }});
  }

  void verifyOperations(
      std::vector<std::string> expectedWarmKeys,
      std::vector<std::string> expectedWarmOps,
      std::vector<std::string> expectedStagingKeys,
      std::vector<std::string> expectedStagingOps) {
    // verify warm side
    EXPECT_EQ(warm_->saw_keys.empty(), expectedWarmKeys.empty());
    EXPECT_EQ(warm_->saw_keys, expectedWarmKeys);

    EXPECT_EQ(warm_->sawOperations.empty(), expectedWarmOps.empty());
    EXPECT_EQ(warm_->sawOperations, expectedWarmOps);

    // verify staging side: should have seen 2 operation: metaget and then add
    // respectively
    EXPECT_EQ(staging_->saw_keys.empty(), expectedStagingKeys.empty());
    EXPECT_EQ(staging_->saw_keys, expectedStagingKeys);

    EXPECT_EQ(staging_->sawOperations.empty(), expectedStagingOps.empty());
    EXPECT_EQ(staging_->sawOperations, expectedStagingOps);
  }

  std::shared_ptr<RouteHandle> rh_;
  std::shared_ptr<TestHandle> warm_;
  std::shared_ptr<TestHandle> staging_;
};

TEST_F(StagingRouteTest, GetTestWarmHitStagingAdd) {
  // test get: hit on warm, miss on stage. --> add to staging
  McGetRequest req("abc");
  createAndRun(
      req,
      GetRouteTestData(mc_res_found, "a"),
      GetRouteTestData(mc_res_notfound, "a"),
      mc_res_found);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc", "abc"},
      std::vector<std::string>{"get", "metaget"},
      /* staging */
      std::vector<std::string>{"abc", "abc"},
      std::vector<std::string>{"metaget", "add"});
}

TEST_F(StagingRouteTest, LeaseGetTestWarmHitStagingAdd) {
  McLeaseGetRequest req("abc");
  createAndRun(
      req,
      GetRouteTestData(mc_res_found, "a"),
      GetRouteTestData(mc_res_notfound, "a"),
      mc_res_found);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc", "abc"},
      std::vector<std::string>{"lease-get", "metaget"},
      /* staging */
      std::vector<std::string>{"abc", "abc"},
      std::vector<std::string>{"metaget", "add"});
}

TEST_F(StagingRouteTest, GetWarmMiss) {
  // test get: miss on warm, hit on stage. --> only see warm miss
  McGetRequest req("abc");
  createAndRun(
      req,
      GetRouteTestData(mc_res_notfound, "a"),
      GetRouteTestData(mc_res_found, "a"),
      mc_res_notfound);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"get"},
      /* staging */
      std::vector<std::string>{},
      std::vector<std::string>{});
}

TEST_F(StagingRouteTest, LeaseGetWarmMiss) {
  // test get: miss on warm, hit on stage. --> only see warm miss
  McLeaseGetRequest req("abc");
  createAndRun(
      req,
      GetRouteTestData(mc_res_notfound, "a"),
      GetRouteTestData(mc_res_found, "a"),
      mc_res_notfound);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"lease-get"},
      /* staging */
      std::vector<std::string>{},
      std::vector<std::string>{});
}

TEST_F(StagingRouteTest, TestSet) {
  // test set: send to both warm/staging
  McSetRequest req("abc");
  createAndRun(
      req,
      UpdateRouteTestData(mc_res_stored, 0),
      UpdateRouteTestData(mc_res_notstored, 0),
      mc_res_stored);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"set"},
      /* staging */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"set"});
}

TEST_F(StagingRouteTest, TestSetFail) {
  // test set fail, staging should not be hit
  McSetRequest req("abc");
  createAndRun(
      req,
      UpdateRouteTestData(mc_res_unknown, 0),
      UpdateRouteTestData(mc_res_stored, 0),
      mc_res_unknown);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"set"},
      /* staging */
      std::vector<std::string>{},
      std::vector<std::string>{});
}

TEST_F(StagingRouteTest, TestLeaseSet) {
  // test lease-set: lease-set on warm, translate to set on staging
  McLeaseSetRequest req("abc");
  createAndRun(
      req,
      UpdateRouteTestData(mc_res_stored, 0),
      UpdateRouteTestData(mc_res_notstored, 0),
      mc_res_stored);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"lease-set"},
      /* staging */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"set"});
}

TEST_F(StagingRouteTest, TestDelete) {
  // test delete: send to both.
  McDeleteRequest req("abc");
  createAndRun(
      req,
      DeleteRouteTestData(mc_res_deleted),
      DeleteRouteTestData(mc_res_unknown),
      mc_res_unknown); // worse reply

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"delete"},
      /* staging */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"delete"});
}

TEST_F(StagingRouteTest, TestCas) {
  McCasRequest req("abc");
  createAndRun(
      req,
      UpdateRouteTestData(mc_res_stored, 0),
      UpdateRouteTestData(mc_res_notstored, 0),
      mc_res_stored);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"cas"},
      /* staging */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"set"});
}

TEST_F(StagingRouteTest, TestGets) {
  McGetsRequest req("abc");
  createAndRun(
      req,
      GetRouteTestData(mc_res_found, "a"),
      GetRouteTestData(mc_res_notfound, "a"),
      mc_res_found);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"gets"},
      /* staging */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"metaget"});
}

TEST_F(StagingRouteTest, TestGetsNotFound) {
  McGetsRequest req("abc");
  createAndRun(
      req,
      GetRouteTestData(mc_res_notfound, "a"),
      GetRouteTestData(mc_res_unknown, "a"),
      mc_res_notfound);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"gets"},
      /* staging */
      std::vector<std::string>{},
      std::vector<std::string>{});
}

TEST_F(StagingRouteTest, TestMetaget) {
  McMetagetRequest req("abc");
  createAndRun(
      req,
      GetRouteTestData(mc_res_found, "a"),
      GetRouteTestData(mc_res_notfound, "a"),
      mc_res_found);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"metaget"},
      /* staging */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"metaget"});
}

TEST_F(StagingRouteTest, TestAddSuccess) {
  // test general operations for non-special cases.
  // route to warm, if successful, async route to staging.
  McAddRequest req("abc");
  createAndRun(
      req,
      UpdateRouteTestData(mc_res_stored, 0),
      UpdateRouteTestData(mc_res_notstored, 0),
      mc_res_stored);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"add"},
      /* staging */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"add"});
}

TEST_F(StagingRouteTest, TestAddFailure) {
  // test general operations for non-special cases.
  // route to warm, if successful, async route to staging.
  McAddRequest req("abc");
  createAndRun(
      req,
      UpdateRouteTestData(mc_res_timeout, 0),
      UpdateRouteTestData(mc_res_stored, 0),
      mc_res_timeout);

  verifyOperations(
      /* warm */
      std::vector<std::string>{"abc"},
      std::vector<std::string>{"add"},
      /* staging */
      std::vector<std::string>{},
      std::vector<std::string>{});
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
