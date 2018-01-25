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

class EagerShardSelector {
 public:
  explicit EagerShardSelector(std::unordered_map<uint32_t, uint32_t> shardsMap)
      : shardsMap_(std::move(shardsMap)) {}

  std::string type() const {
    return "basic-shard-selector";
  }

  template <class Request>
  size_t select(const Request& req, size_t /* size */) const {
    auto dest = shardsMap_.find(req.shardId());
    if (dest == shardsMap_.end()) {
      // if the shard is not found in the map, return a value outside of range
      // of valid destinations (i.e. >= size), so that we error the request.
      return std::numeric_limits<size_t>::max();
    }
    return dest->second;
  }

 private:
  const std::unordered_map<uint32_t, uint32_t> shardsMap_;
};

class EagerShardSelectionRouteTest
    : public ShardSelectionRouteTestUtil<HelloGoodbyeRouterInfo> {
 public:
  HelloGoodbyeRouterInfo::RouteHandlePtr getEagerShardSelectionRoute(
      folly::StringPiece jsonStr) {
    return createEagerShardSelectionRoute<
        HelloGoodbyeRouterInfo,
        EagerShardSelector>(rhFactory_, folly::parseJson(jsonStr));
  }

  void testCreate(folly::StringPiece config) {
    auto rh = getEagerShardSelectionRoute(config);
    ASSERT_TRUE(rh);
    EXPECT_EQ("selection|basic-shard-selector", rh->routeName());
  }
};

TEST_F(EagerShardSelectionRouteTest, createPools) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "children_type": "LoadBalancerRoute",
    "pools": [
      {
        "pool": {
          "type": "Pool",
          "name": "pool1",
          "servers": [ "localhost:12345", "localhost:12312" ],
          "protocol": "caret"
        },
        "shards": [
          "1, 2, 3",
          "4, 5, 6"
        ]
      },
      {
        "pool": {
          "type": "Pool",
          "name": "pool2",
          "servers": [ "localhost:12349", "localhost:12352" ],
          "protocol": "caret"
        },
        "shards": [
          [1, 2, 3],
          [4, 5, 6]
        ]
      }
    ],
    "children_settings" : {
      "load_ttl_ms": 100
    }
  }
  )";

  try {
    testCreate(kSelectionRouteConfig);
  } catch (const std::exception& e) {
    FAIL() << "Configuration failed, but should have succeeded. Exception: "
           << e.what();
  }
}

TEST_F(EagerShardSelectionRouteTest, createMissingHost) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "children_type": "LoadBalancerRoute",
    "pools": [
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
    ],
    "children_settings" : {
      "load_ttl_ms": 100
    }
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
        "EagerShardSelectionRoute: 'shards' must have the same number of entries "
        "as servers in 'pool'",
        errorMsg);
  }
}

TEST_F(EagerShardSelectionRouteTest, createMissingHostPools) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "children_type": "LoadBalancerRoute",
    "pools": [
      {
        "pool": {
          "type": "Pool",
          "name": "pool1",
          "servers": [ "localhost:12345", "localhost:12325" ],
          "protocol": "caret"
        },
        "shards": [
          "1, 2, 3",
          "3, 5, 6"
        ]
      },
      {
        "pool": {
          "type": "Pool",
          "name": "pool2",
          "servers": [ ],
          "protocol": "caret"
        },
        "shards": [ ]
      }
    ],
    "children_settings" : {
      "load_ttl_ms": 100
    }
  }
  )";

  // should configure fine because the first pool is still valid
  try {
    testCreate(kSelectionRouteConfig);
  } catch (const std::exception& e) {
    FAIL() << "Configuration failed, but should have succeeded. Exception: "
           << e.what();
  }
}

TEST_F(EagerShardSelectionRouteTest, routeArray) {
  constexpr folly::StringPiece kSelectionRouteConfig = R"(
  {
    "children_type": "LoadBalancerRoute",
    "pools": [
      {
        "pool": [
          "NullRoute",
          "ErrorRoute"
        ],
        "shards": [
          [1, 2, 3],
          [2, 3, 6]
        ]
      }
    ],
    "children_settings" : {
      "load_ttl_ms": 1000000,
      "default_server_load_percent": 99
    }
  }
  )";

  auto rh = getEagerShardSelectionRoute(kSelectionRouteConfig);
  ASSERT_TRUE(rh);
  EXPECT_EQ("selection|basic-shard-selector", rh->routeName());

  GoodbyeRequest req;
  GoodbyeReply reply;

  req.shardId() = 1;
  size_t cnt = 0;
  RouteHandleTraverser<HelloGoodbyeRouterInfo::RouteHandleIf> t{
      [&cnt](const HelloGoodbyeRouterInfo::RouteHandleIf& r) {
        ++cnt;
        if (cnt == 1) {
          EXPECT_EQ("loadbalancer", r.routeName());
        }
      }};
  rh->traverse(req, t);
  EXPECT_GE(cnt, 1);

  req.shardId() = 1;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 2;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  // load hasnt changed much, still NullRoute
  req.shardId() = 2;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 3;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  // load hasnt changed much, still ErrorRoute
  req.shardId() = 3;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_notfound, reply.result());

  req.shardId() = 6;
  reply = rh->route(req);
  EXPECT_EQ(mc_res_local_error, reply.result());
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
