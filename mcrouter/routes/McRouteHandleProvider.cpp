/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "McRouteHandleProvider.h"

#include <folly/Range.h>

#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/routes/ExtraRouteHandleProviderIf.h"
#include "mcrouter/routes/RateLimiter.h"
#include "mcrouter/routes/ShadowRouteIf.h"
#include "mcrouter/routes/ShardHashFunc.h"
#include "mcrouter/routes/ShardSplitter.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeAsynclogRoute(McrouterRouteHandlePtr rh,
                                         std::string poolName);

McrouterRouteHandlePtr makeDestinationRoute(
  std::shared_ptr<const ProxyClientCommon> client,
  std::shared_ptr<ProxyDestination> destination);

McrouterRouteHandlePtr makeDevNullRoute(const char* name);

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json);

McrouterRouteHandlePtr makeMigrateRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json);

McrouterRouteHandlePtr makeOperationSelectorRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json);

McrouterRouteHandlePtr makeRateLimitRoute(
  McrouterRouteHandlePtr normalRoute,
  RateLimiter rateLimiter);

McrouterRouteHandlePtr makeShardSplitRoute(McrouterRouteHandlePtr rh,
                                           ShardSplitter);

McrouterRouteHandlePtr makeWarmUpRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json,
  uint32_t exptime);

McRouteHandleProvider::McRouteHandleProvider(
  proxy_t* proxy,
  ProxyDestinationMap& destinationMap,
  PoolFactory& poolFactory)
    : RouteHandleProvider<McrouterRouteHandleIf>(),
      proxy_(proxy),
      destinationMap_(destinationMap),
      poolFactory_(poolFactory),
      extraProvider_(createExtraRouteHandleProvider()) {
}

McRouteHandleProvider::~McRouteHandleProvider() {
  /* Needed for forward declaration of ExtraRouteHandleProviderIf in .h */
}

McrouterRouteHandlePtr McRouteHandleProvider::makeDestinationHandle(
    std::shared_ptr<const ProxyClientCommon> client) {
  assert(client);

  auto pdstn = destinationMap_.fetch(*client);

  auto route = makeDestinationRoute(client, std::move(pdstn));
  return destinationHandles_[std::move(client)] = std::move(route);
}

std::vector<McrouterRouteHandlePtr>
McRouteHandleProvider::getDestinationHandlesForPool(
    std::shared_ptr<const ProxyPool> pool) {

  assert(pool);

  std::vector<McrouterRouteHandlePtr> destinations;
  for (auto& client : pool->clients) {
    auto poolClient = client.lock();
    auto it = destinationHandles_.find(poolClient);
    auto destination = (it == destinationHandles_.end())
      ? makeDestinationHandle(poolClient)
      : it->second;

    destinations.push_back(destination);
  }
  return destinations;
}

std::vector<McrouterRouteHandlePtr>
McRouteHandleProvider::makePool(const folly::dynamic& json) {
  checkLogic(json.isString(), "Pool should be a string (name of pool)");
  auto pool = poolFactory_.fetchPool(json.asString());
  checkLogic(pool != nullptr, "Pool not found: {}", json.asString());

  auto proxyPool = std::dynamic_pointer_cast<ProxyPool>(pool);
  checkLogic(proxyPool != nullptr, "Only regional/regular pools are supported");

  auto existingIt = poolHandles_.find(proxyPool);
  if (existingIt != poolHandles_.end()) {
    return existingIt->second;
  }

  return poolHandles_[proxyPool] = getDestinationHandlesForPool(proxyPool);
}

McrouterRouteHandlePtr McRouteHandleProvider::makePoolRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  checkLogic(json.isObject() || json.isString(),
             "PoolRoute should be object or string");
  std::string poolName;
  std::shared_ptr<ProxyGenericPool> pool;
  if (json.isObject()) {
    checkLogic(json.count("pool"), "PoolRoute: no pool");
    const auto& jPool = json["pool"];
    if (jPool.isString()) {
      poolName = jPool.asString().toStdString();
      pool = poolFactory_.fetchPool(poolName);
    } else {
      checkLogic(jPool.count("name") && jPool["name"].isString(),
                 "PoolRoute: no/invalid pool name");
      poolName = jPool["name"].asString().toStdString();
      pool = poolFactory_.fetchPool(poolName);
      if (!pool) {
        pool = poolFactory_.parsePool(poolName, jPool, {});
      }
    }
  } else {
    poolName = json.asString().toStdString();
    pool = poolFactory_.fetchPool(poolName);
  }
  checkLogic(pool != nullptr, "Pool not found: {}", poolName);
  auto proxyPool = std::dynamic_pointer_cast<ProxyPool>(pool);
  checkLogic(proxyPool != nullptr, "Only regional/regular pools are supported");

  std::vector<McrouterRouteHandlePtr> destinations;
  auto existingIt = poolHandles_.find(proxyPool);
  if (existingIt != poolHandles_.end()) {
    destinations = existingIt->second;
  } else {
    destinations = getDestinationHandlesForPool(proxyPool);
    poolHandles_[proxyPool] = destinations;
  }

  if (json.isObject() && json.count("shadows")) {
    std::string shadowPolicy = "default";
    if (json.count("shadow_policy")) {
      checkLogic(json["shadow_policy"].isString(),
                 "PoolRoute: shadow_policy is not string");
      shadowPolicy = json["shadow_policy"].asString().toStdString();
    }

    McrouterShadowData data;
    for (auto& shadow : json["shadows"]) {
      checkLogic(shadow.count("target"),
                 "PoolRoute shadows: no target for shadow");
      auto policy = std::make_shared<ShadowSettings>(shadow, proxy_->router);
      data.emplace_back(factory.create(shadow["target"]), std::move(policy));
    }

    for (size_t i = 0; i < destinations.size(); ++i) {
      destinations[i] = extraProvider_->makeShadow(
        proxy_, destinations[i], data, i, shadowPolicy);
    }
  }

  McrouterRouteHandlePtr route;
  if (json.isObject() && json.count("hash")) {
    if (json["hash"].isString()) {
      route = makeHash(folly::dynamic::object("hash_func", json["hash"]),
                       std::move(destinations));
    } else {
      route = makeHash(json["hash"], std::move(destinations));
    }
  } else {
    route = makeHash(folly::dynamic::object(), std::move(destinations));
  }

  if (proxy_->opts.destination_rate_limiting) {
    if (json.isObject() &&
        json.count("rates") &&
        json["rates"].isObject()) {
      route = makeRateLimitRoute(std::move(route), RateLimiter(json["rates"]));
    } else if (proxyPool->rate_limiter) {
      route = makeRateLimitRoute(std::move(route), *proxyPool->rate_limiter);
    }
  }

  if (json.isObject() && json.count("shard_splits")) {
    route = makeShardSplitRoute(std::move(route),
                                ShardSplitter(json["shard_splits"]));
  } else if (proxyPool->shardSplitter) {
    route = makeShardSplitRoute(std::move(route), *proxyPool->shardSplitter);
  }

  if (!proxy_->opts.asynclog_disable) {
    bool needAsynclog = !proxyPool->devnull_asynclog;
    if (json.isObject() && json.count("asynclog")) {
      checkLogic(json["asynclog"].isBool(), "PoolRoute: asynclog is not bool");
      needAsynclog = json["asynclog"].asBool();
    }
    if (needAsynclog) {
      route = makeAsynclogRoute(std::move(route), proxyPool->getName());
    }
  }
  // NOTE: we assume PoolRoute is unique for each ProxyPool.
  // Once we have multiple PoolRoutes for same ProxyPool
  // we need to change logic here.
  asyncLogRoutes_.emplace(proxyPool->getName(), route);

  return route;
}

std::vector<McrouterRouteHandlePtr> McRouteHandleProvider::create(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    folly::StringPiece type,
    const folly::dynamic& json) {

  // PrefixPolicyRoute if deprecated, but must be preserved for backwards
  // compatibility.
  if (type == "OperationSelectorRoute" || type == "PrefixPolicyRoute") {
    return { makeOperationSelectorRoute(factory, json) };
  } else if (type == "DevNullRoute") {
    return { makeDevNullRoute("devnull") };
  } else if (type == "FailoverWithExptimeRoute") {
    return { makeFailoverWithExptimeRoute(factory, json) };
  } else if (type == "WarmUpRoute") {
    return { makeWarmUpRoute(factory, json,
                             proxy_->opts.upgrading_l1_exptime) };
  } else if (type == "MigrateRoute") {
    return { makeMigrateRoute(factory, json) };
  } else if (type == "Pool") {
    return makePool(json);
  } else if (type == "PoolRoute") {
    return { makePoolRoute(factory, json) };
  }

  auto ret = RouteHandleProvider<McrouterRouteHandleIf>::create(factory, type,
                                                                json);
  checkLogic(!ret.empty(), "Unknown RouteHandle: {}", type);
  return ret;
}

McrouterRouteHandlePtr McRouteHandleProvider::createHash(
    folly::StringPiece funcType,
    const folly::dynamic& json,
    std::vector<McrouterRouteHandlePtr> children) {

  if (funcType == ConstShardHashFunc::type()) {
    return makeRouteHandle<McrouterRouteHandleIf, HashRoute,
                           ConstShardHashFunc>(
      json, std::move(children));
  }

  auto ret = RouteHandleProvider<McrouterRouteHandleIf>::createHash(
    funcType, json, std::move(children));
  checkLogic(ret != nullptr, "Unknown hash function: {}", funcType);
  return ret;
}

}}} // facebook::memcache::mcrouter
