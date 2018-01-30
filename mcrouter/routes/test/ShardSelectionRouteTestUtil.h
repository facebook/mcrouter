/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include <folly/Range.h>
#include <folly/Singleton.h>
#include <folly/json.h>

#include <mcrouter/CarbonRouterInstance.h>
#include <mcrouter/PoolFactory.h>
#include <mcrouter/lib/config/RouteHandleFactory.h> // @manual=//mcrouter/lib/config:routing_config
#include <mcrouter/options.h>
#include <mcrouter/routes/McRouteHandleProvider.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouterInfo>
class ShardSelectionRouteTestUtil : public ::testing::Test {
 public:
  ShardSelectionRouteTestUtil()
      : router_(memcache::mcrouter::CarbonRouterInstance<RouterInfo>::init(
            "testRouter",
            getOpts())),
        poolFactory_(folly::dynamic::object(), router_->configApi()),
        rhProvider_(*router_->getProxy(0), poolFactory_),
        rhFactory_(rhProvider_, 0) {}

  virtual ~ShardSelectionRouteTestUtil() {}

  typename RouterInfo::RouteHandlePtr getRoute(folly::StringPiece jsonStr) {
    return rhFactory_.create(memcache::parseJsonString(jsonStr.str()));
  }

  void tearDown() {
    folly::SingletonVault::singleton()->destroyInstances();
    folly::SingletonVault::singleton()->reenableInstances();
  }

 protected:
  memcache::mcrouter::CarbonRouterInstance<RouterInfo>* router_;
  memcache::mcrouter::PoolFactory poolFactory_;
  memcache::mcrouter::McRouteHandleProvider<RouterInfo> rhProvider_;
  memcache::RouteHandleFactory<typename RouterInfo::RouteHandleIf> rhFactory_;

  static memcache::McrouterOptions getOpts() {
    // Dummy config, used just to spin up mcrouter.
    constexpr folly::StringPiece kDummyConfig = R"(
      {
        "pools": {
          "A": {
            "servers": ["localhost:5000"],
            "protocol": "caret"
          }
        },
        "route": "Pool|A"
      }
      )";
    memcache::McrouterOptions opts;
    opts.num_proxies = 1;
    opts.stats_logging_interval = 0;
    opts.config = kDummyConfig.str();
    return opts;
  }
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
