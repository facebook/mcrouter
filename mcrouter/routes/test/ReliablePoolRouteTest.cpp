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

#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/routes/ReliablePoolRoute.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;
using std::shared_ptr;
using std::string;
using std::vector;

static int counter = 0;
class HashFunc {
 public:
  explicit HashFunc(size_t n) : n_(n) {}

  size_t operator()(folly::StringPiece key) const {
    return counter++ % n_;
  }

 private:
  size_t n_;
};

TEST(ReliablePoolRouteTest, firstHostSuccess) {
  counter = 0;
  vector<std::shared_ptr<TestHandle>> saltedHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
  };

  TestFiberManager fm;

  TestRouteHandle<ReliablePoolRoute<TestRouteHandleIf, HashFunc>> rh(
    get_route_handles(saltedHandle),
    HashFunc(saltedHandle.size()),
    "", 5);

  auto reply = rh.routeSimple(McRequest("key"),
                              McOperation<mc_op_get>());

  EXPECT_EQ(reply.result(), mc_res_found);
  EXPECT_EQ(toString(reply.value()), "a");
  EXPECT_TRUE(saltedHandle[0]->saw_keys == vector<std::string>{"key"});
  EXPECT_TRUE(saltedHandle[1]->saw_keys.empty());
  EXPECT_TRUE(saltedHandle[2]->saw_keys.empty());
}

TEST(ReliablePoolRouteTest, failoverOnce) {
  counter = 0;
  vector<std::shared_ptr<TestHandle>> saltedHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_notfound, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
  };

  TestFiberManager fm;

  TestRouteHandle<ReliablePoolRoute<TestRouteHandleIf, HashFunc>> rh(
    get_route_handles(saltedHandle),
    HashFunc(saltedHandle.size()),
    "", 5);

  auto reply = rh.routeSimple(McRequest("key"),
                              McOperation<mc_op_get>());

  EXPECT_EQ(reply.result(), mc_res_notfound);
  EXPECT_EQ(toString(reply.value()), "b");
  EXPECT_TRUE(saltedHandle[0]->saw_keys == vector<std::string>{"key"});
  EXPECT_TRUE(saltedHandle[1]->saw_keys == vector<std::string>{"key"});
  EXPECT_TRUE(saltedHandle[2]->saw_keys.empty());
}

TEST(ReliablePoolRouteTest, failoverTwice) {
  counter = 0;
  vector<std::shared_ptr<TestHandle>> saltedHandle{
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "a")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_timeout, "b")),
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "c")),
  };

  TestFiberManager fm;

  TestRouteHandle<ReliablePoolRoute<TestRouteHandleIf, HashFunc>> rh(
    get_route_handles(saltedHandle),
    HashFunc(saltedHandle.size()),
    "", 5);

  auto reply = rh.routeSimple(McRequest("key"),
                              McOperation<mc_op_get>());

  EXPECT_EQ(reply.result(), mc_res_found);
  EXPECT_EQ(toString(reply.value()), "c");
  EXPECT_TRUE(saltedHandle[0]->saw_keys == vector<std::string>{"key"});
  EXPECT_TRUE(saltedHandle[1]->saw_keys == vector<std::string>{"key"});
  EXPECT_TRUE(saltedHandle[2]->saw_keys == vector<std::string>{"key"});
}

TEST(ReliablePoolRouteTest, deleteOps) {
  counter = 0;
  vector<std::shared_ptr<TestHandle>> saltedHandle{
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_found)),
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_notfound)),
    make_shared<TestHandle>(DeleteRouteTestData(mc_res_found)),
  };

  TestFiberManager fm;

  fm.runAll(
    {
      [&] () {
        TestRouteHandle<ReliablePoolRoute<TestRouteHandleIf, HashFunc>> rh(
          get_route_handles(saltedHandle),
          HashFunc(saltedHandle.size()),
          "", 5);

        auto reply = rh.routeSimple(McRequest("key"),
                                    McOperation<mc_op_delete>());

        // Get the most awfull reply
        EXPECT_EQ(reply.result(), mc_res_notfound);
      }
    });
  EXPECT_TRUE(saltedHandle[0]->saw_keys == (vector<std::string>{"key", "key"}));
  EXPECT_TRUE(saltedHandle[1]->saw_keys == (vector<std::string>{"key", "key"}));
  EXPECT_TRUE(saltedHandle[2]->saw_keys == (vector<std::string>{"key", "key"}));
}
