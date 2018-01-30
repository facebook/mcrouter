/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <vector>

#include <gtest/gtest.h>

#include <folly/Range.h>
#include <folly/json.h>

#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/lib/carbon/example/gen/HelloGoodbyeRouterInfo.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/options.h"
#include "mcrouter/routes/McRouteHandleProvider.h"
#include "mcrouter/routes/ShardSelectionRouteFactory.h"
#include "mcrouter/routes/test/ShardSelectionRouteTestUtil.h"

using namespace facebook::memcache::mcrouter;
using namespace hellogoodbye;

namespace facebook {
namespace memcache {
namespace mcrouter {

class BasicShardSelector {
 public:
  explicit BasicShardSelector(std::vector<uint16_t> shardsMap)
      : shardsMap_(std::move(shardsMap)) {}

  std::string type() const {
    return "basic-shard-selector";
  }

  template <class Request>
  size_t select(const Request& req, size_t /* size */) const {
    size_t shardId = req.shardId();
    if (shardId >= shardsMap_.size()) {
      // if the shard is not found in the map, return a value outside of range
      // of valid destinations (i.e. >= size), so that we error the request.
      return std::numeric_limits<size_t>::max();
    }
    return shardsMap_.at(shardId);
  }

 private:
  const std::vector<uint16_t> shardsMap_;
};

class ShardSelectionRouteTest
    : public ShardSelectionRouteTestUtil<HelloGoodbyeRouterInfo> {
 public:
  HelloGoodbyeRouterInfo::RouteHandlePtr getShardSelectionRoute(
      folly::StringPiece jsonStr) {
    return createShardSelectionRoute<
        HelloGoodbyeRouterInfo,
        BasicShardSelector>(rhFactory_, folly::parseJson(jsonStr));
  }

  void testCreate(folly::StringPiece config) {
    auto rh = getShardSelectionRoute(config);
    ASSERT_TRUE(rh);
    EXPECT_EQ("selection|basic-shard-selector", rh->routeName());
  }
};

TEST_F(ShardSelectionRouteTest, create) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": {
      "type": "Pool",
      "name": "pool1",
      "servers": [ "localhost:12345", "localhost:12312" ],
      "protocol": "caret"
    },
    "shards": [
      [1, 2, 3],
      [4, 5, 6]
    ]
  }
  )";

  testCreate(kSelectionRouteConfig);
}

TEST_F(ShardSelectionRouteTest, createString) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": {
      "type": "Pool",
      "name": "pool1",
      "servers": [ "localhost:12345", "localhost:12312" ],
      "protocol": "caret"
    },
    "shards": [
      "1, 2, 3",
      "4, 5, 6,"
    ]
  }
  )";

  testCreate(kSelectionRouteConfig);
}

TEST_F(ShardSelectionRouteTest, createDuplicateShard) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": {
      "type": "Pool",
      "name": "pool1",
      "servers": [ "localhost:12345", "localhost:12312" ],
      "protocol": "caret"
    },
    "shards": [
      [1, 2, 3],
      [3, 5, 6]
    ]
  }
  )";

  // should successfully create the route handle, even with duplicate shards
  try {
    testCreate(kSelectionRouteConfig);
  } catch (const std::exception& e) {
    FAIL() << "Configuration failed, but should have succeeded. Exception: "
           << e.what();
  }
}

TEST_F(ShardSelectionRouteTest, createDuplicateShardString) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": {
      "type": "Pool",
      "name": "pool1",
      "servers": [ "localhost:12345", "localhost:12312" ],
      "protocol": "caret"
    },
    "shards": [
      "1, 2, 3",
      "3, 5, 6"
    ]
  }
  )";

  // should successfully create the route handle, even with duplicate shards
  try {
    testCreate(kSelectionRouteConfig);
  } catch (const std::exception& e) {
    FAIL() << "Configuration failed, but should have succeeded. Exception: "
           << e.what();
  }
}

TEST_F(ShardSelectionRouteTest, createMissingHost) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": {
      "type": "Pool",
      "name": "pool1",
      "servers": [ "localhost:12345" ],
      "protocol": "caret"
    },
    "shards": [
      [1, 2, 3],
      [3, 5, 6]
    ]
  }
  )";

  // should throw, because we have one extra entry in "shards" array
  // when compared to "servers" array.
  try {
    testCreate(kSelectionRouteConfig);
    FAIL() << "Config is invalid (there's one missing host)."
           << " Should have thrown.";
  } catch (const std::exception& e) {
    std::string errorMsg = e.what();
    EXPECT_EQ(
        "ShardSelectionRoute: 'shards' must have the same number of entries "
        "as servers in 'pool'",
        errorMsg);
  }
}

TEST_F(ShardSelectionRouteTest, createMissingHostString) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": {
      "type": "Pool",
      "name": "pool1",
      "servers": [ "localhost:12345" ],
      "protocol": "caret"
    },
    "shards": [
      "1, 2, 3",
      "3, 5, 6"
    ]
  }
  )";

  // should throw, because we have one extra entry in "shards" array
  // when compared to "servers" array.
  try {
    testCreate(kSelectionRouteConfig);
    FAIL() << "Config is invalid (there's one missing host)."
           << " Should have thrown.";
  } catch (const std::exception& e) {
    std::string errorMsg = e.what();
    EXPECT_EQ(
        "ShardSelectionRoute: 'shards' must have the same number of entries "
        "as servers in 'pool'",
        errorMsg);
  }
}

TEST_F(ShardSelectionRouteTest, createMissingShardList) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": {
      "type": "Pool",
      "name": "pool1",
      "servers": [ "localhost:12345", "localhost:12312" ],
      "protocol": "caret"
    },
    "shards": [
      [1, 2, 3]
    ]
  }
  )";

  // should throw, because we have one extra entry in "servers" array
  // when compared to "shards" array.
  try {
    testCreate(kSelectionRouteConfig);
    FAIL() << "Config is invalid (there's one missing shard list)."
           << " Should have thrown.";
  } catch (const std::exception& e) {
    std::string errorMsg = e.what();
    EXPECT_EQ(
        "ShardSelectionRoute: 'shards' must have the same number of entries "
        "as servers in 'pool'",
        errorMsg);
  }
}

TEST_F(ShardSelectionRouteTest, createMissingShardListString) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": {
      "type": "Pool",
      "name": "pool1",
      "servers": [ "localhost:12345", "localhost:12312" ],
      "protocol": "caret"
    },
    "shards": [
      "1, 2, 3"
    ]
  }
  )";

  // should throw, because we have one extra entry in "servers" array
  // when compared to "shards" array.
  try {
    testCreate(kSelectionRouteConfig);
    FAIL() << "Config is invalid (there's one shard list)."
           << " Should have thrown.";
  } catch (const std::exception& e) {
    std::string errorMsg = e.what();
    EXPECT_EQ(
        "ShardSelectionRoute: 'shards' must have the same number of entries "
        "as servers in 'pool'",
        errorMsg);
  }
}

TEST_F(ShardSelectionRouteTest, createInvalidShardList) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": {
      "type": "Pool",
      "name": "pool1",
      "servers": [ 
        "localhost:11111",
        "localhost:22222",
        "localhost:33333",
        "localhost:44444",
        "localhost:55555"
      ],
      "protocol": "caret"
    },
    "shards": [
      "",
      "*",
      ",",
      "1,*,3,",
      ",,,,2,,,,,,,,,,5"
    ]
  }
  )";

  // should throw, because we have a broken list of shards.
  try {
    testCreate(kSelectionRouteConfig);
    FAIL() << "Config is invalid (there's one shard list)."
           << " Should have thrown.";
  } catch (const std::exception& e) {
    std::string errorMsg = e.what();
    EXPECT_EQ(
        "ShardSelectionRoute: 'shards' property expected to be a string of "
        "comma-separated integers. Invalid shard found in string: *. "
        "Exception: Non-digit character found: \"*\"",
        errorMsg);
  }
}

TEST_F(ShardSelectionRouteTest, createValidShardList) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": {
      "type": "Pool",
      "name": "pool1",
      "servers": [ 
        "localhost:11111",
        "localhost:22222",
        "localhost:33333",
        "localhost:44444"
      ],
      "protocol": "caret"
    },
    "shards": [
      "",
      "1,2,",
      "3,4",
      "5,"
    ]
  }
  )";

  // should successfully create the route handle, even with
  // trailing commas and empty strings.
  try {
    testCreate(kSelectionRouteConfig);
  } catch (const std::exception& e) {
    FAIL() << "Configuration failed, but should have succeeded. Exception: "
           << e.what();
  }
}

TEST_F(ShardSelectionRouteTest, route) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": [
      "NullRoute",
      "ErrorRoute"
    ],
    "shards": [
      [1, 3, 5],
      [2, 4, 6]
    ]
  }
  )";

  auto rh = getShardSelectionRoute(kSelectionRouteConfig);
  ASSERT_TRUE(rh);

  GoodbyeRequest req;
  GoodbyeReply reply;

  req.shardId() = 1;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 2;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_local_error, reply.result());

  req.shardId() = 3;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 4;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_local_error, reply.result());

  req.shardId() = 5;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 6;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_local_error, reply.result());
}

TEST_F(ShardSelectionRouteTest, routeString) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": [
      "NullRoute",
      "ErrorRoute"
    ],
    "shards": [
      "1, 3, 5",
      "2, 4, 6"
    ]
  }
  )";

  auto rh = getShardSelectionRoute(kSelectionRouteConfig);
  ASSERT_TRUE(rh);

  GoodbyeRequest req;
  GoodbyeReply reply;

  req.shardId() = 1;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 2;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_local_error, reply.result());

  req.shardId() = 3;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 4;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_local_error, reply.result());

  req.shardId() = 5;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 6;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_local_error, reply.result());
}

TEST_F(ShardSelectionRouteTest, outOfRange) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": [
      "NullRoute",
      "NullRoute"
    ],
    "shards": [
      [1],
      [2]
    ]
  }
  )";

  auto rh = getShardSelectionRoute(kSelectionRouteConfig);
  ASSERT_TRUE(rh);

  GoodbyeRequest req;
  GoodbyeReply reply;

  req.shardId() = 1;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 2;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 3;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_local_error, reply.result());
}

TEST_F(ShardSelectionRouteTest, outOfRangeString) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": [
      "NullRoute",
      "NullRoute"
    ],
    "shards": [
      "1",
      "2"
    ]
  }
  )";

  auto rh = getShardSelectionRoute(kSelectionRouteConfig);
  ASSERT_TRUE(rh);

  GoodbyeRequest req;
  GoodbyeReply reply;

  req.shardId() = 1;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 2;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 3;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_local_error, reply.result());
}

TEST_F(ShardSelectionRouteTest, customOutOfRangeRoute) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": [
      "NullRoute",
      "NullRoute"
    ],
    "shards": [
      [1],
      [2]
    ],
    "out_of_range": "ErrorRoute|Cool message!"
  }
  )";

  auto rh = getShardSelectionRoute(kSelectionRouteConfig);
  ASSERT_TRUE(rh);

  GoodbyeRequest req;
  GoodbyeReply reply;

  req.shardId() = 1;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 2;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 3;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_local_error, reply.result());
  EXPECT_EQ("Cool message!", reply.message());
}

TEST_F(ShardSelectionRouteTest, customOutOfRangeRouteString) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "pool": [
      "NullRoute",
      "NullRoute"
    ],
    "shards": [
      "1",
      "2"
    ],
    "out_of_range": "NullRoute"
  }
  )";

  auto rh = getShardSelectionRoute(kSelectionRouteConfig);
  ASSERT_TRUE(rh);

  GoodbyeRequest req;
  GoodbyeReply reply;

  req.shardId() = 1;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 2;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 3;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
