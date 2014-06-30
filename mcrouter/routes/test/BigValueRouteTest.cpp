/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "mcrouter/routes/BigValueRoute.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;

using std::make_shared;
using std::string;
using std::vector;

static const int version = 1;
static const int threshold = 128;
static const BigValueRouteOptions options(threshold);

TEST(BigValueRouteTest, smallvalue) {
  // for small values, this route handle simply passes it to child route handle
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(mc_res_found, "a"),
                            UpdateRouteTestData(mc_res_stored),
                            DeleteRouteTestData(mc_res_deleted))
  };
  auto route_handles = get_route_handles(test_handles);
  TestFiberManager fm;

  fm.runAll({
    [&]() {
      TestRouteHandle<BigValueRoute<TestRouteHandleIf>> rh(
        route_handles[0], options);

      string key = "key_get";
      auto msg = createMcMsgRef(key, "value");
      msg->op = mc_op_get;
      McRequest req_get(std::move(msg));
      auto f_get = rh.route(req_get, McOperation<mc_op_get>());

      EXPECT_TRUE(toString(f_get.value()) == "a");
      EXPECT_TRUE(test_handles[0]->saw_keys == vector<string>{"key_get"});
      (test_handles[0]->saw_keys).clear();

      string key_set = "key_set";
      auto msg_set = createMcMsgRef(key_set, "value");
      msg_set->op = mc_op_set;
      McRequest req_set(std::move(msg_set));
      auto f_set = rh.route(req_set, McOperation<mc_op_set>());
      EXPECT_TRUE(toString(f_set.value()) == "value");
      EXPECT_TRUE(test_handles[0]->saw_keys == vector<string>{"key_set"});
    }
  });
}

TEST(BigValueRouteTest, bigvalue) {
  // for big values, used saw_keys of test route handle to verify that
  // get path and set path saw original key and chunk keys in correct sequesne.

  std::string rand_suffix_get ("123456");
  int num_chunks = 10;
  // initial reply of the form version-num_chunks-rand_suffix for get path
  std::string init_reply =
    folly::format("{}-{}-{}", version, num_chunks, rand_suffix_get).str();
  std::string init_reply_error =
    folly::format("{}-{}", version, num_chunks).str();
  vector<std::shared_ptr<TestHandle>> test_handles{
    make_shared<TestHandle>(GetRouteTestData(
          mc_res_found, init_reply, MC_MSG_FLAG_BIG_VALUE)),
    make_shared<TestHandle>(GetRouteTestData(
          mc_res_found, init_reply_error, MC_MSG_FLAG_BIG_VALUE)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored)),
    make_shared<TestHandle>(UpdateRouteTestData(mc_res_stored))
  };
  auto route_handles = get_route_handles(test_handles);

  TestFiberManager fm;

  fm.runAll({
    [&]() {
      { // Test Get Like path with init_reply in corect format
        TestRouteHandle<BigValueRoute<TestRouteHandleIf>> rh(
          route_handles[0], options);

        auto msg = createMcMsgRef("key_get");
        msg->op = mc_op_get;
        McRequest req_get(std::move(msg));

        auto f_get = rh.route(req_get, McOperation<mc_op_get>());
        auto keys_get = test_handles[0]->saw_keys;
        EXPECT_TRUE(keys_get.size() == num_chunks + 1);
        // first get the result for original key
        EXPECT_TRUE(keys_get.front() == "key_get");

        std::string merged_str;
        // since reply for first key indicated that it is for a big get request,
        // perform get request on chunk keys
        for (int i = 1; i < num_chunks + 1; i++) {
          auto chunk_key = folly::format(
            "key_get:{}:{}", i-1, rand_suffix_get).str();
          EXPECT_TRUE(keys_get[i] == chunk_key);
          merged_str.append(init_reply);
        }

        // each chunk_key saw value as init_reply.
        // In GetLike path, it gets appended num_chunks time
        EXPECT_TRUE(toString(f_get.value()) == merged_str);
      }

      { // Test Get Like path with init_reply_error
        TestRouteHandle<BigValueRoute<TestRouteHandleIf>> rh(
          route_handles[1], options);

        auto msg = createMcMsgRef("key_get");
        msg->op = mc_op_get;
        McRequest req_get(std::move(msg));

        auto f_get = rh.route(req_get, McOperation<mc_op_get>());
        auto keys_get = test_handles[1]->saw_keys;
        EXPECT_TRUE(keys_get.size() == 1);
        // first get the result for original key, then return mc_res_notfound
        EXPECT_TRUE(keys_get.front() == "key_get");
        EXPECT_TRUE(f_get.result() == mc_res_notfound);
        EXPECT_TRUE(toString(f_get.value()) == "");
      }

      { // Test Update Like path with mc_op_set op
        TestRouteHandle<BigValueRoute<TestRouteHandleIf>> rh(
          route_handles[2], options);

        std::string big_value  = folly::to<std::string>(
          std::string(threshold*(num_chunks/2), 't'),
          std::string(threshold*(num_chunks/2), 's'));
        std::string chunk_type_1 = std::string(threshold, 't');
        std::string chunk_type_2 = std::string(threshold, 's');
        auto msg_set = createMcMsgRef("key_set", big_value);
        msg_set->op = mc_op_set;
        McRequest req_set(std::move(msg_set));

        auto f_set = rh.route(req_set, McOperation<mc_op_set>());
        auto keys_set = test_handles[2]->saw_keys;
        auto values_set = test_handles[2]->sawValues;
        EXPECT_TRUE(keys_set.size() == num_chunks + 1);
        std::string rand_suffix_set;
        // first set chunk values corresponding to chunk keys
        for(int i = 0; i < num_chunks; i++) {
          auto chunk_key_prefix = folly::format("key_set:{}:", i).str();
          auto length = chunk_key_prefix.length();
          auto saw_prefix = keys_set[i].substr(0, length);
          EXPECT_TRUE(saw_prefix == chunk_key_prefix);

          if (rand_suffix_set.empty()) { // rand_suffic same for all chunk_keys
            rand_suffix_set = keys_set[i].substr(length);
          } else {
            EXPECT_TRUE(rand_suffix_set == keys_set[i].substr(length));
          }

          if (i < num_chunks/2) {
            EXPECT_TRUE(values_set[i] == chunk_type_1);
          } else {
            EXPECT_TRUE(values_set[i] == chunk_type_2);
          }
        }

        // if set for chunk keys succeed,
        // set original key with chunks info as modified value
        EXPECT_TRUE(keys_set[num_chunks] == "key_set");
        auto chunks_info = folly::format(
            "{}-{}-{}", version, num_chunks, rand_suffix_set).str();
        EXPECT_TRUE(values_set[num_chunks] == chunks_info);
        EXPECT_TRUE(toString(f_set.value()) == values_set[num_chunks]);
      }

      { // Test Update Like path with mc_op_lease_set op
        TestRouteHandle<BigValueRoute<TestRouteHandleIf>> rh(
          route_handles[3], options);

        std::string big_value  = folly::to<std::string>(
          std::string(threshold*(num_chunks/2), 't'),
          std::string(threshold*(num_chunks/2), 's'));
        auto msg_set = createMcMsgRef("key_set", big_value);
        msg_set->op = mc_op_lease_set;
        McRequest req_set(std::move(msg_set));

        auto f_set = rh.route(req_set, McOperation<mc_op_lease_set>());
        auto keys_set = test_handles[3]->saw_keys;
        auto operations_set = test_handles[3]->sawOperations;
        EXPECT_TRUE(keys_set.size() == num_chunks + 1);
        std::string rand_suffix_set;
        // first set chunk values corresponding to chunk keys
        for(int i = 0; i < num_chunks; i++) {
          auto chunk_key_prefix = folly::format("key_set:{}:", i).str();
          auto length = chunk_key_prefix.length();
          auto saw_prefix = keys_set[i].substr(0, length);
          EXPECT_TRUE(saw_prefix == chunk_key_prefix);

          EXPECT_TRUE(operations_set[i] == mc_op_set);
        }

        // if set for chunk keys succeed,
        // set original key with chunks info as modified value
        EXPECT_TRUE(keys_set[num_chunks] == "key_set");
        EXPECT_TRUE(operations_set[num_chunks] == mc_op_lease_set);
      }
    }
  });
}
