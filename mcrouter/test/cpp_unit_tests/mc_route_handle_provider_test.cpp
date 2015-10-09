/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <folly/io/async/EventBase.h>
#include <folly/json.h>
#include <folly/Memory.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/options.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/McRouteHandleProvider.h"
#include "mcrouter/test/cpp_unit_tests/mcrouter_cpp_tests.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

namespace {

const char* const kAsynclogRoute =
 R"({
  "type": "AsynclogRoute",
  "name": "test",
  "target": "NullRoute"
 })";

const char* const kConstShard =
 R"({
  "type": "HashRoute",
  "children": "ErrorRoute",
  "hash_func": "ConstShard"
 })";

const char* const kInvalidHashFunc =
 R"({
  "type": "HashRoute",
  "children": "ErrorRoute",
  "hash_func": "InvalidHashFunc"
 })";

const char* const kWarmUp =
 R"({
   "type": "WarmUpRoute",
   "cold": "ErrorRoute",
   "warm": "NullRoute"
 })";

const char* const kPoolRoute =
 R"({
   "type": "PoolRoute",
   "pool": { "name": "mock", "servers": [ ] },
   "hash": { "hash_func": "Crc32" }
 })";

struct TestSetup {
 public:
  TestSetup()
    : router_(McrouterInstance::init("test_get_route", getOpts())),
      poolFactory_(folly::dynamic::object(),
                   router_->configApi(),
                   router_->opts()),
      rhProvider_(*router_->getProxy(0), poolFactory_),
      rhFactory_(rhProvider_) {
  }

  McRouteHandleProvider& provider() {
    return rhProvider_;
  }

  McrouterRouteHandlePtr getRoute(const char* jsonStr) {
    return rhFactory_.create(parseJsonString(jsonStr));
  }
 private:
  McrouterInstance* router_;
  PoolFactory poolFactory_;
  McRouteHandleProvider rhProvider_;
  RouteHandleFactory<McrouterRouteHandleIf> rhFactory_;

  static McrouterOptions getOpts() {
    auto opts = defaultTestOptions();
    opts.config_file = kMemcacheConfig;
    return opts;
  }
};

}  // anonymous

TEST(McRouteHandleProviderTest, asynclog_route) {
  TestSetup setup;
  auto rh = setup.getRoute(kAsynclogRoute);
  EXPECT_TRUE(rh != nullptr);
  EXPECT_EQ("asynclog:test", rh->routeName());
  auto asynclogRoutes = setup.provider().releaseAsyncLogRoutes();
  EXPECT_EQ(1, asynclogRoutes.size());
  EXPECT_EQ("asynclog:test", asynclogRoutes["test"]->routeName());
}

TEST(McRouteHandleProviderTest, sanity) {
  auto rh = TestSetup().getRoute(kConstShard);
  EXPECT_TRUE(rh != nullptr);
  EXPECT_EQ("error", rh->routeName());
}

TEST(McRouteHandleProviderTest, invalid_func) {
  try {
    auto rh = TestSetup().getRoute(kInvalidHashFunc);
  } catch (const std::logic_error& e) {
    return;
  }
  FAIL() << "No exception thrown";
}

TEST(McRouteHandleProvider, warmup) {
  auto rh = TestSetup().getRoute(kWarmUp);
  EXPECT_TRUE(rh != nullptr);
  EXPECT_EQ("warm-up", rh->routeName());
}

TEST(McRouteHandleProvider, pool_route) {
  TestSetup setup;
  auto rh = setup.getRoute(kPoolRoute);
  EXPECT_TRUE(rh != nullptr);
  EXPECT_EQ("asynclog:mock", rh->routeName());
  auto asynclogRoutes = setup.provider().releaseAsyncLogRoutes();
  EXPECT_EQ(1, asynclogRoutes.size());
  EXPECT_EQ("asynclog:mock", asynclogRoutes["mock"]->routeName());
}
