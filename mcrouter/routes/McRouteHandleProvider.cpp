/*
 *  Copyright (c) 2016, Facebook, Inc.
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

#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/ParsingUtil.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/proxy.h"
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
  std::shared_ptr<ProxyDestination> destination,
  std::string poolName,
  size_t indexInPool,
  std::chrono::milliseconds timeout,
  bool keepRoutingPrefix);

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

McrouterRouteHandlePtr
McRouteHandleProvider::createAsynclogRoute(McrouterRouteHandlePtr target,
                                           std::string asynclogName) {
  if (!proxy_.router().opts().asynclog_disable) {
    target = makeAsynclogRoute(std::move(target), asynclogName);
  }
  asyncLogRoutes_.emplace(std::move(asynclogName), target);
  return target;
}

const std::vector<McrouterRouteHandlePtr>&
McRouteHandleProvider::makePool(McRouteHandleFactory& factory,
                                const PoolFactory::PoolJson& jpool) {
  auto existingIt = pools_.find(jpool.name);
  if (existingIt != pools_.end()) {
    return existingIt->second;
  }

  auto name = jpool.name.str();
  const auto& json = jpool.json;
  auto& opts = proxy_.router().opts();
  // region & cluster
  folly::StringPiece region, cluster;
  if (auto jregion = json.get_ptr("region")) {
    if (!jregion->isString()) {
      MC_LOG_FAILURE(opts, memcache::failure::Category::kInvalidConfig,
                     "Pool {}: pool_region is not a string", name);
    } else {
      region = jregion->stringPiece();
    }
  }
  if (auto jcluster = json.get_ptr("cluster")) {
    if (!jcluster->isString()) {
      MC_LOG_FAILURE(opts, memcache::failure::Category::kInvalidConfig,
                     "Pool {}: pool_cluster is not a string", name);
    } else {
      cluster = jcluster->stringPiece();
    }
  }

  try {
    std::chrono::milliseconds timeout{opts.server_timeout_ms};
    if (auto jTimeout = json.get_ptr("server_timeout")) {
      timeout = parseTimeout(*jTimeout, "server_timeout");
    }

    if (!region.empty() && !cluster.empty()) {
      auto& route = opts.default_route;
      if (region == route.getRegion() && cluster == route.getCluster()) {
        if (opts.within_cluster_timeout_ms != 0) {
          timeout = std::chrono::milliseconds(opts.within_cluster_timeout_ms);
        }
      } else if (region == route.getRegion()) {
        if (opts.cross_cluster_timeout_ms != 0) {
          timeout = std::chrono::milliseconds(opts.cross_cluster_timeout_ms);
        }
      } else {
        if (opts.cross_region_timeout_ms != 0) {
          timeout = std::chrono::milliseconds(opts.cross_region_timeout_ms);
        }
      }
    }

    mc_protocol_t protocol = mc_ascii_protocol;
    if (auto jProtocol = json.get_ptr("protocol")) {
      auto str = parseString(*jProtocol, "protocol");
      if (equalStr("ascii", str, folly::AsciiCaseInsensitive())) {
        protocol = mc_ascii_protocol;
      } else if (equalStr("caret", str, folly::AsciiCaseInsensitive())) {
        protocol = mc_caret_protocol;
      } else if (equalStr("umbrella", str, folly::AsciiCaseInsensitive())) {
        protocol = mc_umbrella_protocol;
      } else {
        throwLogic("Unknown protocol '{}'", str);
      }
    }

    bool keepRoutingPrefix = false;
    if (auto jKeepRoutingPrefix = json.get_ptr("keep_routing_prefix")) {
      keepRoutingPrefix = parseBool(*jKeepRoutingPrefix, "keep_routing_prefix");
    }

    uint64_t qosClass = opts.default_qos_class;
    uint64_t qosPath = opts.default_qos_path;
    if (auto jQos = json.get_ptr("qos")) {
      checkLogic(jQos->isObject(), "qos must be an object.");
      if (auto jClass = jQos->get_ptr("class")) {
        qosClass = parseInt(*jClass, "qos.class", 0, 4);
      }
      if (auto jPath = jQos->get_ptr("path")) {
        qosPath = parseInt(*jPath, "qos.path", 0, 3);
      }
    }

    bool useSsl = false;
    if (auto jUseSsl = json.get_ptr("use_ssl")) {
      useSsl = parseBool(*jUseSsl, "use_ssl");
    }

    // default to 0, which doesn't override
    uint16_t port = 0;
    if (auto jPort = json.get_ptr("port_override")) {
      port = parseInt(*jPort, "port_override", 1, 65535);
    }
    // servers
    auto jservers = json.get_ptr("servers");
    checkLogic(jservers, "servers not found");
    checkLogic(jservers->isArray(), "servers is not an array");
    std::vector<McrouterRouteHandlePtr> destinations;
    destinations.reserve(jservers->size());
    for (size_t i = 0; i < jservers->size(); ++i) {
      const auto& server = jservers->at(i);
      checkLogic(server.isString() || server.isObject(),
                 "server #{} is not a string/object", i);
      if (server.isObject()) {
        destinations.push_back(factory.create(server));
        continue;
      }
      auto ap = AccessPoint::create(server.stringPiece(), protocol, useSsl,
                                    port);
      checkLogic(ap != nullptr, "invalid server {}", server.stringPiece());

      accessPoints_[name].push_back(ap);

      auto pdstn = proxy_.destinationMap->find(*ap, timeout);
      if (!pdstn) {
        pdstn = proxy_.destinationMap->emplace(
          std::move(ap), timeout, qosClass, qosPath
        );
      }
      pdstn->updatePoolName(name);
      pdstn->updateShortestTimeout(timeout);

      destinations.push_back(makeDestinationRoute(
        std::move(pdstn), name, i, timeout, keepRoutingPrefix));
    } // servers

    return pools_.emplace(name, std::move(destinations)).first->second;
  } catch (const std::exception& e) {
    throwLogic("Pool {}: {}", name, e.what());
  }
}

McrouterRouteHandlePtr
McRouteHandleProvider::makePoolRoute(McRouteHandleFactory& factory,
                                     const folly::dynamic& json) {
  checkLogic(json.isObject() || json.isString(),
             "PoolRoute should be object or string");
  const folly::dynamic* jpool;
  if (json.isObject()) {
    jpool = json.get_ptr("pool");
    checkLogic(jpool, "PoolRoute: pool not found");
  } else { // string
    jpool = &json;
  }

  auto poolJson = poolFactory_.parsePool(*jpool);
  auto destinations = makePool(factory, poolJson);

  try {
    if (json.isObject()) {
      if (auto maxOutstandingPtr = json.get_ptr("max_outstanding")) {
        auto v = parseInt(*maxOutstandingPtr, "max_outstanding", 0, 1000000);
        if (v) {
          for (auto& destination: destinations) {
            destination = makeOutstandingLimitRoute(std::move(destination), v);
          }
        }
      }
      if (auto slowWarmUpJson = json.get_ptr("slow_warmup")) {
        checkLogic(slowWarmUpJson->isObject(),
                   "slow_warmup must be a json object");

        auto failoverTargetJson = slowWarmUpJson->get_ptr("failoverTarget");
        checkLogic(failoverTargetJson,
                   "couldn't find 'failoverTarget' property in slow_warmup");
        auto failoverTarget = factory.create(*failoverTargetJson);

        std::shared_ptr<SlowWarmUpRouteSettings> slowWarmUpSettings;
        if (auto settingsJson = slowWarmUpJson->get_ptr("settings")) {
          checkLogic(settingsJson->isObject(),
                     "'settings' in slow_warmup must be a json object.");
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

      if (json.count("shadows")) {
        destinations = makeShadowRoutes(
            factory, json, std::move(destinations), proxy_, *extraProvider_);
      }
    }

    // add weights and override whatever we have in PoolRoute::hash
    folly::dynamic jhashWithWeights = folly::dynamic::object();
    if (auto jWeights = poolJson.json.get_ptr("weights")) {
      jhashWithWeights = folly::dynamic::object
        ("hash_func", WeightedCh3HashFunc::type())
        ("weights", *jWeights);
    }

    if (json.isObject()) {
      if (auto jhash = json.get_ptr("hash")) {
        checkLogic(jhash->isObject() || jhash->isString(),
                   "hash is not object/string");
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

    auto asynclogName = poolJson.name;
    bool needAsynclog = true;
    if (json.isObject()) {
      if (proxy_.router().opts().destination_rate_limiting) {
        if (auto jrates = json.get_ptr("rates")) {
          route = makeRateLimitRoute(std::move(route), RateLimiter(*jrates));
        }
      }
      if (auto jsplits = json.get_ptr("shard_splits")) {
        route = makeShardSplitRoute(std::move(route), ShardSplitter(*jsplits));
      }
      if (auto jasynclog = json.get_ptr("asynclog")) {
        needAsynclog = parseBool(*jasynclog, "asynclog");
      }
      if (auto jname = json.get_ptr("name")) {
        asynclogName = parseString(*jname, "name");
      }
    }
    if (needAsynclog) {
      route = createAsynclogRoute(std::move(route), asynclogName.str());
    }

    return route;
  } catch (const std::exception& e) {
    throwLogic("PoolRoute {}: {}", poolJson.name, e.what());
  }
}

std::vector<McrouterRouteHandlePtr> McRouteHandleProvider::create(
    McRouteHandleFactory& factory,
    folly::StringPiece type,
    const folly::dynamic& json) {

  if (type == "Pool") {
    return makePool(factory, poolFactory_.parsePool(json));
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

  throwLogic("Unknown RouteHandle: {}", type);
}

}}} // facebook::memcache::mcrouter
