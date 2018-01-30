/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/routes/ShardSplitRoute.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

namespace facebook {
namespace memcache {
namespace mcrouter {
namespace test {

using FiberManagerContextTag =
    typename fiber_local<MemcacheRouterInfo>::ContextTypeTag;

constexpr size_t kNumSplits = 26 * 26 + 1;

template <class Request, class RouterInfo = MemcacheRouterInfo>
void testShardingForOp(ShardSplitter splitter, uint64_t requestFlags = 0) {
  using ShardSplitTestHandle =
      TestHandleImpl<typename RouterInfo::RouteHandleIf>;
  using ShardSplitRouteHandle =
      typename RouterInfo::template RouteHandle<ShardSplitRoute<RouterInfo>>;

  for (size_t i = 0; i < kNumSplits; ++i) {
    globals::HostidMock hostidMock(i);

    std::vector<std::shared_ptr<ShardSplitTestHandle>> handles{
        std::make_shared<ShardSplitTestHandle>(
            GetRouteTestData(mc_res_found, "a"),
            UpdateRouteTestData(mc_res_found),
            DeleteRouteTestData(mc_res_found))};
    auto rh = get_route_handles(handles)[0];
    ShardSplitRouteHandle splitRoute(rh, splitter);

    TestFiberManager fm{FiberManagerContextTag()};
    fm.run([&] {
      mockFiberContext();
      Request req("test:123:");
      req.flags() = requestFlags;
      auto reply = splitRoute.route(req);
      EXPECT_EQ(mc_res_found, reply.result());
    });

    if (i == 0) {
      EXPECT_EQ(std::vector<std::string>{"test:123:"}, handles[0]->saw_keys);
    } else {
      EXPECT_EQ(
          std::vector<std::string>{folly::sformat(
              "test:123{}{}:",
              (char)('a' + (i - 1) % 26),
              (char)('a' + (i - 1) / 26))},
          handles[0]->saw_keys);
    }
  }
}

} // test
} // mcrouter
} // memcache
} // facebook
