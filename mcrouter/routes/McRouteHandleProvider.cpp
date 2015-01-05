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

#include <folly/Range.h>

#include "mcrouter/ClientPool.h"
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

std::pair<std::shared_ptr<ClientPool>, std::vector<McrouterRouteHandlePtr>>
McRouteHandleProvider::makePool(const folly::dynamic& json) {
  checkLogic(json.isString() || json.isObject(),
             "Pool should be a string (name of pool) or an object");
  auto pool = poolFactory_.parsePool(json);
  auto seenIt = pools_.find(pool->getName());
  if (seenIt != pools_.end()) {
    return seenIt->second;
  }

  std::vector<McrouterRouteHandlePtr> destinations;
  for (const auto& client : pool->getClients()) {
    auto pdstn = destinationMap_.fetch(*client);
    auto route = makeDestinationRoute(client, std::move(pdstn));
    destinations.push_back(std::move(route));
  }

  return std::make_pair(std::move(pool), std::move(destinations));
}

McrouterRouteHandlePtr McRouteHandleProvider::makePoolRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

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

  if (json.isObject() && json.count("shadows")) {
    folly::StringPiece shadowPolicy = "default";
    if (auto jshadow_policy = json.get_ptr("shadow_policy")) {
      checkLogic(jshadow_policy->isString(),
                 "PoolRoute: shadow_policy is not a string");
      shadowPolicy = jshadow_policy->stringPiece();
    }

    McrouterShadowData data;
    for (auto& shadow : json["shadows"]) {
      checkLogic(shadow.count("target"),
                 "PoolRoute {} shadows: no target for shadow", pool->getName());
      auto policy = std::make_shared<ShadowSettings>(shadow, proxy_->router);
      data.emplace_back(factory.create(shadow["target"]), std::move(policy));
    }

    for (size_t i = 0; i < destinations.size(); ++i) {
      destinations[i] = extraProvider_->makeShadow(
        proxy_, std::move(destinations[i]), data, i, shadowPolicy);
    }
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
  auto route = makeHash(jhashWithWeights, std::move(destinations));

  if (json.isObject()) {
    if (proxy_->opts.destination_rate_limiting) {
      if (auto jrates = json.get_ptr("rates")) {
        route = makeRateLimitRoute(std::move(route), RateLimiter(*jrates));
      }
    }

    if (auto jsplits = json.get_ptr("shard_splits")) {
      route = makeShardSplitRoute(std::move(route), ShardSplitter(*jsplits));
    }
  }

  if (!proxy_->opts.asynclog_disable) {
    bool needAsynclog = true;
    if (json.isObject() && json.count("asynclog")) {
      checkLogic(json["asynclog"].isBool(), "PoolRoute: asynclog is not bool");
      needAsynclog = json["asynclog"].getBool();
    }
    if (needAsynclog) {
      route = makeAsynclogRoute(std::move(route), pool->getName());
    }
  }
  // NOTE: we assume PoolRoute is unique for each ProxyPool.
  // Once we have multiple PoolRoutes for same ProxyPool
  // we need to change logic here.
  asyncLogRoutes_.emplace(pool->getName(), route);

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
    return makePool(json).second;
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
