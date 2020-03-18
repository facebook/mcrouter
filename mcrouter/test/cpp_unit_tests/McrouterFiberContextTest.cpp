/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gtest/gtest.h>

#include "folly/fibers/FiberManagerMap.h"
#include "folly/io/async/EventBase.h"
#include "mcrouter/McrouterFiberContext.h"

using namespace ::testing;

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace {
const int64_t kNetworkTransportTimeUs = 100;

template <typename LocalTypeTag>
struct ExtractLocalType {};

template <typename T>
struct ExtractLocalType<folly::fibers::LocalType<T>> {
  using type = T;
};

template <typename LocalTypeTag>
using ExtractLocalTypeT = typename ExtractLocalType<LocalTypeTag>::type;

struct RouterInfo {};
} // namespace

TEST(McrouterFiberContextTest, setAndGetNetworkTransportTimeUs) {
  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManagerT<
      ExtractLocalTypeT<fiber_local<RouterInfo>::ContextTypeTag>>(evb);
  fm.addTask([]() {
    EXPECT_EQ(fiber_local<RouterInfo>::getNetworkTransportTimeUs(), 0);
    fiber_local<RouterInfo>::incNetworkTransportTimeBy(kNetworkTransportTimeUs);
    EXPECT_EQ(
        fiber_local<RouterInfo>::getNetworkTransportTimeUs(),
        kNetworkTransportTimeUs);

    folly::fibers::addTask([] {
      EXPECT_EQ(
          fiber_local<RouterInfo>::getNetworkTransportTimeUs(),
          kNetworkTransportTimeUs);
      fiber_local<RouterInfo>::incNetworkTransportTimeBy(
          kNetworkTransportTimeUs);

      folly::fibers::addTask([]() {
        EXPECT_EQ(
            fiber_local<RouterInfo>::getNetworkTransportTimeUs(),
            2 * kNetworkTransportTimeUs);
      });
    });
  });
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
