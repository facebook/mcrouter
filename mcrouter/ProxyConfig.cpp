#include "ProxyConfig.h"

#include <functional>

#include "folly/Conv.h"
#include "folly/dynamic.h"
#include "folly/json.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/ServiceInfo.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/BigValueRouteIf.h"
#include "mcrouter/routes/McRouteHandleProvider.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/PrefixRouteSelector.h"
#include "mcrouter/routes/ProxyRoute.h"
#include "mcrouter/routes/RouteSelectorMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeBigValueRoute(McrouterRouteHandlePtr ch,
                                         BigValueRouteOptions options);

namespace {

void addRouteSelector(const folly::dynamic& aliases,
                      const folly::dynamic& route,
                      RouteHandleFactory<McrouterRouteHandleIf>& factory,
                      RouteSelectorMap& routeSelectors) {

  auto routeSelector = std::make_shared<PrefixRouteSelector>(factory, route);
  for (const auto& alias : aliases) {
    checkLogic(alias.isString(), "Alias is not string");
    auto key = alias.asString().toStdString();
    if (routeSelectors.routes.count(key)) {
      routeSelectors.routes[key] = routeSelector;
    } else {
      routeSelectors.routes.emplace(key, routeSelector);
    }
  }
}

McrouterRouteHandlePtr
attachRootHandles(proxy_t* proxy, McrouterRouteHandlePtr root) {
  if (proxy->opts.big_value_split_threshold != 0) {
    BigValueRouteOptions options(proxy->opts.big_value_split_threshold);
    return makeBigValueRoute(std::move(root), std::move(options));
  }
  return root;
}

}  // anonymous namespace

ProxyConfig::ProxyConfig(proxy_t* proxy,
                         const folly::dynamic& json,
                         std::string configMd5Digest,
                         std::shared_ptr<PoolFactory> poolFactory)
  : poolFactory_(std::move(poolFactory)),
    configMd5Digest_(std::move(configMd5Digest)) {

  McRouteHandleProvider provider(proxy, *proxy->destinationMap, *poolFactory_);
  RouteHandleFactory<McrouterRouteHandleIf> factory(
    provider,
    std::bind(attachRootHandles, proxy, std::placeholders::_1));

  checkLogic(json.isObject(), "Config is not object");

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
    addRouteSelector({ proxy->default_route }, json["route"], factory,
                     routeSelectors);
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

  for (const auto& it : poolFactory_->pools()) {
    const auto& pool = it.second;
    if (pool->getType() == REGIONAL_POOL || pool->getType() == REGULAR_POOL) {
      auto handle = provider.getPoolHandle(pool->getName());
      if (handle) {
        routeSelectors.pools.emplace(pool->getName(), std::move(handle));
      }
    }
  }

  proxyRoute_ = std::make_shared<ProxyRoute>(proxy, routeSelectors);
  serviceInfo_ = std::make_shared<ServiceInfo>(proxy, *this);
}

}}} // facebook::memcache::mcrouter
