/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyConfig.h"

#include <folly/Conv.h>
#include <folly/dynamic.h>
#include <folly/json.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/McRouteHandleProvider.h"
#include "mcrouter/PoolFactory.h"
#include "mcrouter/routes/PrefixRouteSelector.h"
#include "mcrouter/routes/ProxyRoute.h"
#include "mcrouter/routes/RouteSelectorMap.h"
#include "mcrouter/ServiceInfo.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

void addRouteSelector(const folly::dynamic& aliases,
                      const folly::dynamic& route,
                      RouteHandleFactory<McrouterRouteHandleIf>& factory,
                      RouteSelectorMap& routeSelectors) {

  auto routeSelector = std::make_shared<PrefixRouteSelector>(factory, route);
  for (const auto& alias : aliases) {
    checkLogic(alias.isString(), "Alias is not string");
    auto key = alias.asString().toStdString();
    if (routeSelectors.count(key)) {
      routeSelectors[key] = routeSelector;
    } else {
      routeSelectors.emplace(key, routeSelector);
    }
  }
}

}  // anonymous namespace

ProxyConfig::ProxyConfig(proxy_t* proxy,
                         const folly::dynamic& json,
                         std::string configMd5Digest,
                         std::shared_ptr<PoolFactory> poolFactory)
  : poolFactory_(std::move(poolFactory)),
    configMd5Digest_(std::move(configMd5Digest)) {

  McRouteHandleProvider provider(proxy, *proxy->destinationMap, *poolFactory_);
  RouteHandleFactory<McrouterRouteHandleIf> factory(provider);

  checkLogic(json.isObject(), "Config is not an object");

  if (json.count("named_handles")) {
    checkLogic(json["named_handles"].isArray(), "named_handles is not array");
    for (const auto& it : json["named_handles"]) {
      factory.create(it);
    }
  }

  checkLogic(!json.count("route") || !json.count("routes"),
             "Config ambiguous, has both route and routes");

  RouteSelectorMap routeSelectors;

  if (json.count("route")) {
    addRouteSelector({ proxy->router().opts().default_route.str() },
                     json["route"], factory, routeSelectors);
  } else if (json.count("routes")) {
    checkLogic(json["routes"].isArray(), "Config: routes is not array");
    for (const auto& it : json["routes"]) {
      checkLogic(it.isObject(), "RoutePolicy is not object");
      checkLogic(it.count("route"), "RoutePolicy has no route");
      checkLogic(it.count("aliases"), "RoutePolicy has no aliases");
      const auto& aliases = it["aliases"];
      checkLogic(aliases.isArray(), "RoutePolicy aliases is not array");
      addRouteSelector(aliases, it["route"], factory, routeSelectors);
    }
  } else {
    throw std::logic_error("No route/routes in config");
  }


  asyncLogRoutes_ = provider.releaseAsyncLogRoutes();
  proxyRoute_ = std::make_shared<ProxyRoute>(proxy, routeSelectors);
  serviceInfo_ = std::make_shared<ServiceInfo>(proxy, *this);
}

McrouterRouteHandlePtr
ProxyConfig::getRouteHandleForAsyncLog(const std::string& asyncLogName) const {
  return tryGet(asyncLogRoutes_, asyncLogName);
}

const std::vector<std::shared_ptr<const ProxyClientCommon>>&
ProxyConfig::getClients() const {
  return poolFactory_->clients();
}

}}} // facebook::memcache::mcrouter
