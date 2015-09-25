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

#include <folly/Conv.h>
#include <folly/Format.h>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/routes/BigValueRoute.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::make_shared;
using std::vector;

using TestHandle = TestHandleImpl<McrouterRouteHandleIf>;

static const int version = 1;
static const int threshold = 128;
static const BigValueRouteOptions opts(threshold, /* batchSize= */ 0);

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
      McrouterRouteHandle<BigValueRoute> rh(route_handles[0], opts);

      std::string key = "key_get";
      auto msg = createMcMsgRef(key, "value");
      msg->op = mc_op_get;
      McRequest req_get(std::move(msg));
      auto f_get = rh.route(req_get, McOperation<mc_op_get>());

      EXPECT_EQ("a", toString(f_get.value()));
      EXPECT_EQ(test_handles[0]->saw_keys, vector<std::string>{"key_get"});
      test_handles[0]->saw_keys.clear();

      std::string key_set = "key_set";
      auto msg_set = createMcMsgRef(key_set, "value");
      msg_set->op = mc_op_set;
      McRequest req_set(std::move(msg_set));
      auto f_set = rh.route(req_set, McOperation<mc_op_set>());
      EXPECT_EQ("value", toString(f_set.value()));
      EXPECT_EQ(test_handles[0]->saw_keys, vector<std::string>{"key_set"});
    }
  });
}

TEST(BigValueRouteTest, bigvalue) {
  // for big values, used saw_keys of test route handle to verify that
  // get path and set path saw original key and chunk keys in correct sequesne.

  const std::string rand_suffix_get = "123456";
  const size_t num_chunks = 10;
  // initial reply of the form version-num_chunks-rand_suffix for get path
  const auto init_reply =
    folly::sformat("{}-{}-{}", version, num_chunks, rand_suffix_get);
  const auto init_reply_error = folly::sformat("{}-{}", version, num_chunks);
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
        McrouterRouteHandle<BigValueRoute> rh(route_handles[0], opts);

        auto msg = createMcMsgRef("key_get");
        msg->op = mc_op_get;
        McRequest req_get(std::move(msg));

        auto f_get = rh.route(req_get, McOperation<mc_op_get>());
        auto keys_get = test_handles[0]->saw_keys;
        EXPECT_EQ(num_chunks + 1, keys_get.size());
        // first get the result for original key
        EXPECT_EQ("key_get", keys_get.front());

        std::string merged_str;
        // since reply for first key indicated that it is for a big get request,
        // perform get request on chunk keys
        for (size_t i = 1; i < num_chunks + 1; i++) {
          auto chunk_key = folly::sformat(
            "key_get:{}:{}", i - 1, rand_suffix_get);
          EXPECT_EQ(keys_get[i], chunk_key);
          merged_str.append(init_reply);
        }

        // each chunk_key saw value as init_reply.
        // In GetLike path, it gets appended num_chunks time
        EXPECT_EQ(merged_str, toString(f_get.value()));
      }

      { // Test Get Like path with init_reply_error
        McrouterRouteHandle<BigValueRoute> rh(route_handles[1], opts);

        auto msg = createMcMsgRef("key_get");
        msg->op = mc_op_get;
        McRequest req_get(std::move(msg));

        auto f_get = rh.route(req_get, McOperation<mc_op_get>());
        auto keys_get = test_handles[1]->saw_keys;
        EXPECT_EQ(1, keys_get.size());
        // first get the result for original key, then return mc_res_notfound
        EXPECT_EQ("key_get", keys_get.front());
        EXPECT_EQ(mc_res_notfound, f_get.result());
        EXPECT_EQ("", toString(f_get.value()));
      }

      { // Test Update Like path with mc_op_set op
        McrouterRouteHandle<BigValueRoute> rh(route_handles[2], opts);

        std::string big_value  = folly::to<std::string>(
          std::string(threshold*(num_chunks/2), 't'),
          std::string(threshold*(num_chunks/2), 's'));
        std::string chunk_type_1(threshold, 't');
        std::string chunk_type_2(threshold, 's');
        auto msg_set = createMcMsgRef("key_set", big_value);
        msg_set->op = mc_op_set;
        McRequest req_set(std::move(msg_set));

        auto f_set = rh.route(req_set, McOperation<mc_op_set>());
        auto keys_set = test_handles[2]->saw_keys;
        auto values_set = test_handles[2]->sawValues;
        EXPECT_EQ(num_chunks + 1, keys_set.size());
        std::string rand_suffix_set;
        // first set chunk values corresponding to chunk keys
        for (size_t i = 0; i < num_chunks; i++) {
          auto chunk_key_prefix = folly::sformat("key_set:{}:", i);
          auto length = chunk_key_prefix.length();
          auto saw_prefix = keys_set[i].substr(0, length);
          EXPECT_EQ(chunk_key_prefix, saw_prefix);

          if (rand_suffix_set.empty()) { // rand_suffic same for all chunk_keys
            rand_suffix_set = keys_set[i].substr(length);
          } else {
            EXPECT_EQ(rand_suffix_set, keys_set[i].substr(length));
          }

          if (i < num_chunks/2) {
            EXPECT_EQ(chunk_type_1, values_set[i]);
          } else {
            EXPECT_EQ(chunk_type_2, values_set[i]);
          }
        }

        // if set for chunk keys succeed,
        // set original key with chunks info as modified value
        EXPECT_EQ("key_set", keys_set[num_chunks]);
        auto chunks_info = folly::sformat(
            "{}-{}-{}", version, num_chunks, rand_suffix_set);
        EXPECT_EQ(chunks_info, values_set[num_chunks]);
        EXPECT_EQ(values_set[num_chunks], toString(f_set.value()));
      }

      { // Test Update Like path with mc_op_lease_set op
        McrouterRouteHandle<BigValueRoute> rh(route_handles[3], opts);

        std::string big_value  = folly::to<std::string>(
          std::string(threshold*(num_chunks/2), 't'),
          std::string(threshold*(num_chunks/2), 's'));
        auto msg_set = createMcMsgRef("key_set", big_value);
        msg_set->op = mc_op_lease_set;
        McRequest req_set(std::move(msg_set));

        auto f_set = rh.route(req_set, McOperation<mc_op_lease_set>());
        auto keys_set = test_handles[3]->saw_keys;
        auto operations_set = test_handles[3]->sawOperations;
        EXPECT_EQ(num_chunks + 1, keys_set.size());
        // first set chunk values corresponding to chunk keys
        for (size_t i = 0; i < num_chunks; i++) {
          auto chunk_key_prefix = folly::sformat("key_set:{}:", i);
          auto length = chunk_key_prefix.length();
          auto saw_prefix = keys_set[i].substr(0, length);
          EXPECT_EQ(chunk_key_prefix, saw_prefix);

          EXPECT_EQ(mc_op_set, operations_set[i]);
        }

        // if set for chunk keys succeed,
        // set original key with chunks info as modified value
        EXPECT_EQ("key_set", keys_set[num_chunks]);
        EXPECT_EQ(mc_op_lease_set, operations_set[num_chunks]);
      }
    }
  });
}
