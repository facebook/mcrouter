/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include <memory>

#include <gtest/gtest.h>

#include "mcrouter/CarbonRouterClient.h"
#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/config.h"

using facebook::memcache::McGetReply;
using facebook::memcache::McGetRequest;
using facebook::memcache::MemcacheRouterInfo;
using facebook::memcache::mcrouter::CarbonRouterInstance;
using facebook::memcache::mcrouter::defaultTestOptions;

TEST(CarbonRouterClient, basicWithNoOpTransport) {
  auto opts = defaultTestOptions();
  opts.num_proxies = 2;
  opts.config_str = R"(
    {
      "pools": {
        "A":{
          "servers": ["127.0.0.1:5001"],
          "protocol": "noop"
        }
      },
      "route": "PoolRoute|A"
    })";

  auto router = CarbonRouterInstance<MemcacheRouterInfo>::init(
      "basicWithNoOpTransport", opts);

  auto client = router->createClient(0 /* max_outstanding_requests */);

  const McGetRequest req("abc");
  bool replyReceived = false;
  folly::fibers::Baton baton;

  client->send(
      req, [&baton, &replyReceived](const McGetRequest&, McGetReply&& reply) {
        // Because we are using noop transport, we should always receive
        // NOTFOUND back.
        EXPECT_EQ(carbon::Result::NOTFOUND, reply.result());
        replyReceived = true;
        baton.post();
      });

  baton.wait();
  router->shutdown();
  ASSERT_TRUE(replyReceived);
}
