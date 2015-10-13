/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McRouteHandleProvider.h"

#include <memory>

#include <folly/Range.h>

#include "mcrouter/ClientPool.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/routes/ExtraRouteHandleProviderIf.h"
#include "mcrouter/routes/RateLimiter.h"
#include "mcrouter/routes/ShadowRouteIf.h"
#include "mcrouter/routes/ShardHashFunc.h"
#include "mcrouter/routes/ShardSplitter.h"
#include "mcrouter/routes/SlowWarmUpRouteSettings.h"

namespace facebook { namespace memcache { namespace mcrouter {

using McRouteHandleFactory = RouteHandleFactory<McrouterRouteHandleIf>;

std::vector<McrouterRouteHandlePtr> makeShadowRoutes(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json,
    std::vector<McrouterRouteHandlePtr> destinations,
    proxy_t& proxy,
    ExtraRouteHandleProviderIf& extraProvider);

std::vector<McrouterRouteHandlePtr> makeShadowRoutes(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json,
    proxy_t& proxy,
    ExtraRouteHandleProviderIf& extraProvider);

McrouterRouteHandlePtr makeAllAsyncRoute(McRouteHandleFactory& factory,
                                         const folly::dynamic& json);

McrouterRouteHandlePtr makeAllFastestRoute(McRouteHandleFactory& factory,
                                           const folly::dynamic& json);

McrouterRouteHandlePtr makeAllInitialRoute(McRouteHandleFactory& factory,
                                           const folly::dynamic& json);

McrouterRouteHandlePtr makeAllMajorityRoute(McRouteHandleFactory& factory,
                                            const folly::dynamic& json);

McrouterRouteHandlePtr makeAllSyncRoute(McRouteHandleFactory& factory,
                                        const folly::dynamic& json);

McrouterRouteHandlePtr makeAsynclogRoute(McrouterRouteHandlePtr rh,
                                         std::string asynclogName);

McrouterRouteHandlePtr makeDevNullRoute(McRouteHandleFactory& factory,
                                        const folly::dynamic& json);

McrouterRouteHandlePtr makeHostIdRoute(McRouteHandleFactory& factory,
                                       const folly::dynamic& json);

McrouterRouteHandlePtr makeDestinationRoute(
  std::shared_ptr<const ProxyClientCommon> client,
  std::shared_ptr<ProxyDestination> destination);

McrouterRouteHandlePtr makeErrorRoute(McRouteHandleFactory& factory,
                                      const folly::dynamic& json);

McrouterRouteHandlePtr makeFailoverRoute(McRouteHandleFactory& factory,
                                         const folly::dynamic& json);

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  McRouteHandleFactory& factory,
  const folly::dynamic& json);

McrouterRouteHandlePtr makeHashRoute(const folly::dynamic& json,
                                     std::vector<McrouterRouteHandlePtr> rh);

McrouterRouteHandlePtr makeHashRoute(McRouteHandleFactory& factory,
                                     const folly::dynamic& json);

McrouterRouteHandlePtr makeL1L2CacheRoute(McRouteHandleFactory& factory,
                                          const folly::dynamic& json);

McrouterRouteHandlePtr makeLatestRoute(McRouteHandleFactory& factory,
                                       const folly::dynamic& json);

McrouterRouteHandlePtr makeLoggingRoute(McRouteHandleFactory& factory,
                                        const folly::dynamic& json);

McrouterRouteHandlePtr makeMigrateRoute(McRouteHandleFactory& factory,
                                        const folly::dynamic& json);

McrouterRouteHandlePtr makeMissFailoverRoute(McRouteHandleFactory& factory,
                                             const folly::dynamic& json);

McrouterRouteHandlePtr makeModifyExptimeRoute(McRouteHandleFactory& factory,
                                              const folly::dynamic& json);

McrouterRouteHandlePtr makeModifyKeyRoute(McRouteHandleFactory& factory,
                                          const folly::dynamic& json);

McrouterRouteHandlePtr makeNullRoute(McRouteHandleFactory& factory,
                                     const folly::dynamic& json);

McrouterRouteHandlePtr makeOperationSelectorRoute(McRouteHandleFactory& factory,
                                                  const folly::dynamic& json);

McrouterRouteHandlePtr makeOutstandingLimitRoute(
  McrouterRouteHandlePtr normalRoute,
  size_t maxOutstanding);

McrouterRouteHandlePtr makeRateLimitRoute(McrouterRouteHandlePtr normalRoute,
                                          RateLimiter rateLimiter);

McrouterRouteHandlePtr makeRateLimitRoute(McRouteHandleFactory& factory,
                                          const folly::dynamic& json);

McrouterRouteHandlePtr makeRandomRoute(McRouteHandleFactory& factory,
                                       const folly::dynamic& json);

McrouterRouteHandlePtr makeShardSplitRoute(McrouterRouteHandlePtr rh,
                                           ShardSplitter);

McrouterRouteHandlePtr makeWarmUpRoute(McRouteHandleFactory& factory,
                                       const folly::dynamic& json);

McrouterRouteHandlePtr makeSlowWarmUpRoute(
    McrouterRouteHandlePtr target,
    McrouterRouteHandlePtr failoverTarget,
    std::shared_ptr<SlowWarmUpRouteSettings> settings);

std::pair<McrouterRouteHandlePtr, std::string> parseAsynclogRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json);

McRouteHandleProvider::McRouteHandleProvider(
  proxy_t& proxy,
  PoolFactory& poolFactory)
    : proxy_(proxy),
      poolFactory_(poolFactory),
      extraProvider_(createExtraRouteHandleProvider()),
      routeMap_{
        { "AllAsyncRoute", &makeAllAsyncRoute },
        { "AllFastestRoute", &makeAllFastestRoute },
        { "AllInitialRoute", &makeAllInitialRoute },
        { "AllMajorityRoute", &makeAllMajorityRoute },
        { "AllSyncRoute", &makeAllSyncRoute },
        {
          "AsynclogRoute",
          [this](McRouteHandleFactory& factory, const folly::dynamic& json) {
            auto p = parseAsynclogRoute(factory, json);
            return createAsynclogRoute(std::move(p.first), std::move(p.second));
          }
        },
        { "DevNullRoute", &makeDevNullRoute },
        { "ErrorRoute", &makeErrorRoute },
        { "FailoverRoute", &makeFailoverRoute },
        { "FailoverWithExptimeRoute", &makeFailoverWithExptimeRoute },
        {
          "HashRoute",
          [](McRouteHandleFactory& factory, const folly::dynamic& json) {
            return makeHashRoute(factory, json);
          }
        },
        { "HostIdRoute", &makeHostIdRoute },
        { "L1L2CacheRoute", &makeL1L2CacheRoute },
        { "LatestRoute", &makeLatestRoute },
        { "LoggingRoute", &makeLoggingRoute },
        { "MigrateRoute", &makeMigrateRoute },
        { "MissFailoverRoute", &makeMissFailoverRoute },
        { "ModifyExptimeRoute", &makeModifyExptimeRoute },
        { "ModifyKeyRoute", &makeModifyKeyRoute },
        { "NullRoute", &makeNullRoute },
        { "OperationSelectorRoute", &makeOperationSelectorRoute },
        {
          "PoolRoute",
          [this](McRouteHandleFactory& factory, const folly::dynamic& json) {
            return makePoolRoute(factory, json);
          }
        },
        { "PrefixPolicyRoute", &makeOperationSelectorRoute },
        { "RandomRoute", &makeRandomRoute },
        {
          "RateLimitRoute",
          [](McRouteHandleFactory& factory, const folly::dynamic& json) {
            return makeRateLimitRoute(factory, json);
          }
        },
        { "WarmUpRoute", &makeWarmUpRoute },
      } {
}

McRouteHandleProvider::~McRouteHandleProvider() {
  /* Needed for forward declaration of ExtraRouteHandleProviderIf in .h */
}

std::pair<std::shared_ptr<ClientPool>, std::vector<McrouterRouteHandlePtr>>
McRouteHandleProvider::makePool(const folly::dynamic& json) {
  checkLogic(json.isString() || json.isObject(),
             "Pool should be a string (name of pool) or an object");
  auto pool = poolFactory_.parsePool(json);
  std::vector<McrouterRouteHandlePtr> destinations;
  for (const auto& client : pool->getClients()) {
    auto pdstn = proxy_.destinationMap->fetch(*client);
    auto route = makeDestinationRoute(client, std::move(pdstn));
    destinations.push_back(std::move(route));
  }

  return std::make_pair(std::move(pool), std::move(destinations));
}

McrouterRouteHandlePtr
McRouteHandleProvider::createAsynclogRoute(McrouterRouteHandlePtr target,
                                           std::string asynclogName) {
  if (!proxy_.router().opts().asynclog_disable) {
    target = makeAsynclogRoute(std::move(target), asynclogName);
  }
  asyncLogRoutes_.emplace(std::move(asynclogName), target);
  return target;
}

McrouterRouteHandlePtr McRouteHandleProvider::makePoolRoute(
  McRouteHandleFactory& factory, const folly::dynamic& json) {

  checkLogic(json.isObject() || json.isString(),
             "PoolRoute should be object or string");
  const folly::dynamic* jpool;
  if (json.isObject()) {
    jpool = json.get_ptr("pool");
    checkLogic(jpool, "PoolRoute: pool not found");
  } else {
    jpool = &json;
  }
  auto p = makePool(*jpool);
  auto pool = std::move(p.first);
  auto destinations = std::move(p.second);

  if (json.isObject()) {
    if (auto maxOutstandingPtr = json.get_ptr("max_outstanding")) {
      checkLogic(maxOutstandingPtr->isInt(),
                 "PoolRoute {}: max_outstanding is not int", pool->getName());
      auto maxOutstanding = maxOutstandingPtr->asInt();
      if (maxOutstanding) {
        for (auto& destination: destinations) {
          destination = makeOutstandingLimitRoute(std::move(destination),
                                                  maxOutstanding);
        }
      }
    }
  }

  if (json.isObject() && json.count("slow_warmup")) {
    auto& slowWarmUpJson = json["slow_warmup"];
    checkLogic(slowWarmUpJson.isObject(), "SlowWarmUp: must be a json object");

    auto failoverTargetJson = slowWarmUpJson.get_ptr("failoverTarget");
    checkLogic(failoverTargetJson,
        "SlowWarmUp: Couldn't find 'failoverTarget' property.");
    auto failoverTarget = factory.create(*failoverTargetJson);

    std::shared_ptr<SlowWarmUpRouteSettings> slowWarmUpSettings;
    if (auto settingsJson = slowWarmUpJson.get_ptr("settings")) {
      checkLogic(settingsJson->isObject(),
          "SlowWarmUp: 'settings' must be a json object.");
      slowWarmUpSettings =
        std::make_shared<SlowWarmUpRouteSettings>(*settingsJson);
    } else {
      slowWarmUpSettings = std::make_shared<SlowWarmUpRouteSettings>();
    }

    for (size_t i = 0; i < destinations.size(); ++i) {
      destinations[i] = makeSlowWarmUpRoute(std::move(destinations[i]),
          failoverTarget, slowWarmUpSettings);
    }
  }

  if (json.isObject() && json.count("shadows")) {
    destinations = makeShadowRoutes(
        factory, json, std::move(destinations), proxy_, *extraProvider_);
  }

  // add weights and override whatever we have in PoolRoute::hash
  folly::dynamic jhashWithWeights = folly::dynamic::object();
  if (pool->getWeights()) {
    jhashWithWeights = folly::dynamic::object
      ("hash_func", WeightedCh3HashFunc::type())
      ("weights", *pool->getWeights());
  }

  if (json.isObject()) {
    if (auto jhash = json.get_ptr("hash")) {
      checkLogic(jhash->isObject() || jhash->isString(),
                 "PoolRoute {}: hash is not object/string", pool->getName());
      if (jhash->isString()) {
        jhashWithWeights["hash_func"] = *jhash;
      } else { // object
        for (const auto& it : jhash->items()) {
          jhashWithWeights[it.first] = it.second;
        }
      }
    }
  }
  auto route = makeHashRoute(jhashWithWeights, std::move(destinations));

  if (json.isObject()) {
    if (proxy_.router().opts().destination_rate_limiting) {
      if (auto jrates = json.get_ptr("rates")) {
        route = makeRateLimitRoute(std::move(route), RateLimiter(*jrates));
      }
    }

    if (auto jsplits = json.get_ptr("shard_splits")) {
      route = makeShardSplitRoute(std::move(route), ShardSplitter(*jsplits));
    }
  }

  auto asynclogName = pool->getName();
  bool needAsynclog = true;
  if (json.isObject()) {
    if (auto jasynclog = json.get_ptr("asynclog")) {
      checkLogic(jasynclog->isBool(), "PoolRoute: asynclog is not bool");
      needAsynclog = jasynclog->getBool();
    }
    if (auto jname = json.get_ptr("name")) {
      checkLogic(jname->isString(), "PoolRoute: name is not a string");
      asynclogName = jname->stringPiece().str();
    }
  }
  if (needAsynclog) {
    route = createAsynclogRoute(std::move(route), asynclogName);
  }

  return route;
}

std::vector<McrouterRouteHandlePtr> McRouteHandleProvider::create(
    McRouteHandleFactory& factory,
    folly::StringPiece type,
    const folly::dynamic& json) {

  if (type == "Pool") {
    return makePool(json).second;
  } else if (type == "ShadowRoute") {
    return makeShadowRoutes(factory, json, proxy_, *extraProvider_);
  }

  auto it = routeMap_.find(type);
  if (it != routeMap_.end()) {
    return { it->second(factory, json) };
  }

  /* returns empty vector if type is unknown */
  auto ret = extraProvider_->tryCreate(factory, type, json);
  if (!ret.empty()) {
    return ret;
  }

  checkLogic(false, "Unknown RouteHandle: {}", type);
  return {};
}

}}} // facebook::memcache::mcrouter
