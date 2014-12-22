/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <folly/io/async/EventBase.h>
#include <folly/json.h>
#include <folly/Memory.h>

#include "mcrouter/_router.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/options.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/McRouteHandleProvider.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

static const std::string kConstShard =
 R"({
  "type": "HashRoute",
  "children": "ErrorRoute",
  "hash_func": "ConstShard"
 })";

static const std::string kWarmUp =
 R"({
   "type": "WarmUpRoute",
   "cold": "ErrorRoute",
   "warm": "NullRoute"
 })";

static const std::string kPoolRoute =
 R"({
   "type": "PoolRoute",
   "pool": { "name": "mock", "servers": [ ] },
   "hash": { "hash_func": "Crc32" }
 })";

static std::shared_ptr<McrouterRouteHandleIf>
getRoute(const folly::dynamic& d) {
  McrouterOptions opts;
  folly::EventBase eventBase;
  auto router = folly::make_unique<mcrouter_t>(opts);
  auto proxy = folly::make_unique<proxy_t>(router.get(), &eventBase, opts);
  PoolFactory pf(folly::dynamic::object(), router->configApi.get(), opts);
  McRouteHandleProvider provider(proxy.get(), *proxy->destinationMap, pf);
  RouteHandleFactory<McrouterRouteHandleIf> factory(provider);
  auto res = factory.create(d);

  // should be disposed before event_base
  proxy.reset();

  return res;
}

TEST(McRouteHandleProviderTest, sanity) {
  auto rh = getRoute(folly::parseJson(kConstShard));
  EXPECT_TRUE(rh != nullptr);
  EXPECT_EQ(rh->routeName(), "hash:ConstShard");
}

TEST(McRouteHandleProviderTest, invalid_func) {
  auto d = folly::parseJson(kConstShard);
  d["hash_func"] = "SomeNotExistingFunc";
  try {
    auto rh = getRoute(d);
  } catch (const std::logic_error& e) {
    return;
  }
  FAIL() << "No exception thrown";
}

TEST(McRouteHandleProvider, warmup) {
  auto rh = getRoute(folly::parseJson(kWarmUp));
  EXPECT_TRUE(rh != nullptr);
  EXPECT_EQ(rh->routeName(), "warm-up");
}

TEST(McRouteHandleProvider, pool_route) {
  auto rh = getRoute(folly::parseJson(kPoolRoute));
  EXPECT_TRUE(rh != nullptr);
  EXPECT_EQ(rh->routeName(), "asynclog");
}
