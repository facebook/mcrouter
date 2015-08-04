/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <folly/Memory.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/routes/AllAsyncRoute.h"
#include "mcrouter/lib/routes/AllFastestRoute.h"
#include "mcrouter/lib/routes/AllInitialRoute.h"
#include "mcrouter/lib/routes/AllMajorityRoute.h"
#include "mcrouter/lib/routes/AllSyncRoute.h"
#include "mcrouter/lib/routes/ErrorRoute.h"
#include "mcrouter/lib/routes/HashRoute.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/lib/test/TestRouteHandle.h"

using namespace facebook::memcache;

using folly::make_unique;
using std::make_shared;
using std::string;
using std::vector;

using TestHandle = TestHandleImpl<TestRouteHandleIf>;

TEST(routeHandleTest, nullGet) {
  TestRouteHandle<NullRoute<TestRouteHandleIf>> rh;
  auto reply = rh.route(McRequest("key"), McOperation<mc_op_get>());
  EXPECT_TRUE(reply.result() == mc_res_notfound);
}

TEST(routeHandleTest, nullSet) {
  TestRouteHandle<NullRoute<TestRouteHandleIf>> rh;
  auto reply = rh.route(McRequest("key"), McOperation<mc_op_set>());
  EXPECT_TRUE(reply.result() == mc_res_notstored);
}

TEST(routeHandleTest, nullDelete) {
  TestRouteHandle<NullRoute<TestRouteHandleIf>> rh;
  auto reply = rh.route(McRequest("key"), McOperation<mc_op_delete>());
  EXPECT_TRUE(reply.result() == mc_res_notfound);
}

TEST(routeHandleTest, nullIncr) {
  TestRouteHandle<NullRoute<TestRouteHandleIf>> rh;
  auto reply = rh.route(McRequest("key"), McOperation<mc_op_incr>());
  EXPECT_TRUE(reply.result() == mc_res_notfound);
}

TEST(routeHandleTest, error) {
  TestRouteHandle<ErrorRoute<TestRouteHandleIf>> rh;
  auto reply = rh.route(McRequest("key"), McOperation<mc_op_get>());
  EXPECT_TRUE(reply.isError());
}

TEST(routeHandleTest, allSync) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "c"))
  };

  TestFiberManager fm;

  TestRouteHandle<AllSyncRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  fm.runAll(
    {
      [&]() {
        auto reply = rh.route(McRequest("key"), McOperation<mc_op_get>());

        /* Check that we got the worst result back */
        EXPECT_TRUE(reply.result() == mc_res_remote_error);
        EXPECT_TRUE(toString(reply.value()) == "c");

        for (auto& h : test_handles) {
          EXPECT_TRUE(h->saw_keys == vector<string>{"key"});
        }
      }
    });
}

TEST(routeHandleTest, allAsync) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "c"))
  };

  TestFiberManager fm;

  TestRouteHandle<AllAsyncRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  fm.runAll(
    {
      [&]() {
        auto op = McOperation<mc_op_get>();
        auto reply = rh.route(McRequest("key"), op);

        /* Check that we got no result back */
        EXPECT_TRUE(reply.result() == mc_res_notfound);
      }
    });

  /* Check that everything is complete in the background */
  for (auto& h : test_handles) {
    EXPECT_TRUE(h->saw_keys == vector<string>{"key"});
  }
}

TEST(routeHandleTest, allInitial) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "c")),
  };

  TestFiberManager fm;
  auto routeHandles = get_route_handles(test_handles);
  TestRouteHandle<AllInitialRoute<TestRouteHandleIf>> rh(routeHandles);

  fm.runAll(
    {
      [&]() {
        auto reply = rh.route(McRequest("key"), McOperation<mc_op_get>());

        /* Check that we got the initial result back */
        EXPECT_TRUE(reply.result() == mc_res_found);
        EXPECT_TRUE(toString(reply.value()) == "a");
      }
    });

  /* Check that everything is complete in the background */
  for (auto& h : test_handles) {
    EXPECT_TRUE(h->saw_keys == vector<string>{"key"});
  }

  /* Check that traverse is correct */
  int cnt = 0;
  RouteHandleTraverser<TestRouteHandleIf> t{
    [&cnt](const TestRouteHandleIf&){ ++cnt; }
  };
  rh.traverse(McRequest("key"), McOperation<mc_op_get>(), t);
  EXPECT_EQ(cnt, routeHandles.size());
}

TEST(routeHandleTest, allMajority) {
  TestFiberManager fm;

  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "c"))
  };

  TestRouteHandle<AllMajorityRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  test_handles[1]->pause();

  fm.runAll(
    {
      [&]() {
        auto reply = rh.route(McRequest("key"), McOperation<mc_op_get>());

        /* Check that we got the majority reply
           without waiting for "b", which is paused */
        EXPECT_TRUE(reply.result() == mc_res_remote_error);

        EXPECT_TRUE(test_handles[0]->saw_keys == vector<string>{"key"});
        EXPECT_TRUE(test_handles[1]->saw_keys == vector<string>{});
        EXPECT_TRUE(test_handles[2]->saw_keys == vector<string>{"key"});

        test_handles[1]->unpause();
      }
    });

  /* Check that everything is complete in the background */
  for (auto& h : test_handles) {
    EXPECT_TRUE(h->saw_keys == vector<string>{"key"});
  }
}

TEST(routeHandleTest, allMajorityTie) {
  TestFiberManager fm;

  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "c")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "d"))
  };

  TestRouteHandle<AllMajorityRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  fm.runAll(
    {
      [&]() {
        auto reply = rh.route(McRequest("key"), McOperation<mc_op_get>());

        /* Check that we got the _worst_ majority reply */
        EXPECT_TRUE(reply.result() == mc_res_remote_error);
      }
    });

  /* Check that everything is complete */
  for (auto& h : test_handles) {
    EXPECT_TRUE(h->saw_keys == vector<string>{"key"});
  }
}

TEST(routeHandleTest, allFastest) {
  TestFiberManager fm;

  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_remote_error, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c"))
  };

  TestRouteHandle<AllFastestRoute<TestRouteHandleIf>> rh(
    get_route_handles(test_handles));

  test_handles[1]->pause();

  fm.runAll(
    {
      [&]() {
        auto reply = rh.route(McRequest("key"), McOperation<mc_op_get>());

        /* Check that we got the fastest non-error result back
           ('b' is paused) */
        EXPECT_TRUE(reply.result() == mc_res_found);
        EXPECT_TRUE(toString(reply.value()) == "c");

        EXPECT_TRUE(test_handles[0]->saw_keys == vector<string>{"key"});
        EXPECT_TRUE(test_handles[1]->saw_keys == vector<string>{});
        EXPECT_TRUE(test_handles[2]->saw_keys == vector<string>{"key"});

        test_handles[1]->unpause();
     }
    });

  /* Check that everything is complete in the background */
  for (auto& h : test_handles) {
    EXPECT_TRUE(h->saw_keys == vector<string>{"key"});
  }
}

class HashFunc {
 public:
  explicit HashFunc(size_t n) : n_(n) {}

  size_t operator()(folly::StringPiece key) const {
    return std::stoi(key.str()) % n_;
  }

  static std::string type() { return "HashFunc"; }

 private:
  size_t n_;
};

TEST(routeHandleTest, hashNoSalt) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
  };

  TestFiberManager fm;

  TestRouteHandle<HashRoute<TestRouteHandleIf, HashFunc>> rh(
    get_route_handles(test_handles),
    /* salt= */ "",
    HashFunc(test_handles.size()));

  fm.run([&]() {
      auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
      EXPECT_TRUE(toString(reply.value()) == "a");
    });

  fm.run([&]() {
      auto reply = rh.route(McRequest("1"), McOperation<mc_op_get>());
      EXPECT_TRUE(toString(reply.value()) == "b");
    });

  fm.run([&]() {
      auto reply = rh.route(McRequest("2"), McOperation<mc_op_get>());
      EXPECT_TRUE(toString(reply.value()) == "c");
    });
}

TEST(routeHandleTest, hashSalt) {
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
  };

  TestFiberManager fm;

  TestRouteHandle<HashRoute<TestRouteHandleIf, HashFunc>> rh(
    get_route_handles(test_handles),
    /* salt= */ "1",
    HashFunc(test_handles.size()));

  fm.run([&]() {
      auto reply = rh.route(McRequest("0"), McOperation<mc_op_get>());
      /* 01 % 3 == 1 */
      EXPECT_TRUE(toString(reply.value()) == "b");
    });

  fm.run([&]() {
      auto reply = rh.route(McRequest("1"), McOperation<mc_op_get>());
      /* 11 % 3 == 2 */
      EXPECT_TRUE(toString(reply.value()) == "c");
    });

  fm.run([&]() {
      auto reply = rh.route(McRequest("2"), McOperation<mc_op_get>());
      /* 21 % 3 == 0 */
      EXPECT_TRUE(toString(reply.value()) == "a");
    });
}
