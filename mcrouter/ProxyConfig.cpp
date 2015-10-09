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
#include "mcrouter/routes/PrefixSelectorRoute.h"
#include "mcrouter/routes/ProxyRoute.h"
#include "mcrouter/routes/RouteSelectorMap.h"
#include "mcrouter/ServiceInfo.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyConfig::ProxyConfig(proxy_t& proxy,
                         const folly::dynamic& json,
                         std::string configMd5Digest,
                         std::shared_ptr<PoolFactory> poolFactory)
  : poolFactory_(std::move(poolFactory)),
    configMd5Digest_(std::move(configMd5Digest)) {

  McRouteHandleProvider provider(proxy, *poolFactory_);
  RouteHandleFactory<McrouterRouteHandleIf> factory(provider);

  checkLogic(json.isObject(), "Config is not an object");

  if (json.count("named_handles")) {
    checkLogic(json["named_handles"].isArray(), "named_handles is not array");
    for (const auto& it : json["named_handles"]) {
      factory.create(it);
    }
  }

  RouteSelectorMap routeSelectors;

  auto jRoute = json.get_ptr("route");
  auto jRoutes = json.get_ptr("routes");
  checkLogic(!jRoute || !jRoutes,
             "Invalid config: both 'route' and 'routes' are specified");
  checkLogic(jRoute || jRoutes, "No route/routes in config");
  if (jRoute) {
    routeSelectors[proxy.getRouterOptions().default_route] =
        std::make_shared<PrefixSelectorRoute>(factory, *jRoute);
  } else { // jRoutes
    checkLogic(jRoutes->isArray() || jRoutes->isObject(),
               "Config: routes is not array/object");
    if (jRoutes->isArray()) {
      for (const auto& it : *jRoutes) {
        checkLogic(it.isObject(), "RoutePolicy is not an object");
        auto jCurRoute = it.get_ptr("route");
        auto jAliases = it.get_ptr("aliases");
        checkLogic(jCurRoute, "RoutePolicy: no route");
        checkLogic(jAliases, "RoutePolicy: no aliases");
        checkLogic(jAliases->isArray(), "RoutePolicy: aliases is not an array");
        auto routeSelector =
            std::make_shared<PrefixSelectorRoute>(factory, *jCurRoute);
        for (const auto& alias : *jAliases) {
          checkLogic(alias.isString(), "RoutePolicy: alias is not a string");
          routeSelectors[alias.stringPiece()] = routeSelector;
        }
      }
    } else { // object
      for (const auto& it : jRoutes->items()) {
        checkLogic(it.first.isString(), "RoutePolicy: alias is not a string");
        routeSelectors[it.first.stringPiece()] =
            std::make_shared<PrefixSelectorRoute>(factory, it.second);
      }
    }
  }

  asyncLogRoutes_ = provider.releaseAsyncLogRoutes();
  proxyRoute_ = std::make_shared<ProxyRoute>(&proxy, routeSelectors);
  serviceInfo_ = std::make_shared<ServiceInfo>(&proxy, *this);
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
