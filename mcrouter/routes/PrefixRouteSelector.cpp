#include "PrefixRouteSelector.h"

#include <map>
#include <utility>

#include "folly/dynamic.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache { namespace mcrouter {

PrefixRouteSelector::PrefixRouteSelector(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  // If json is not PrefixRouteSelector, use it as wildcard RouteHandle.
  // NOTE: "route" is deprecated and will go away soon
  if (!json.isObject() || !json.count("type") || !json["type"].isString() ||
      (json["type"].asString() != "route" &&
       json["type"].asString() != "PrefixSelectorRoute")) {
    wildcard = factory.createRoot(json);
    return;
  }

  if (json.count("policies")) {
    const auto& jpolicies = json["policies"];
    checkLogic(jpolicies.isObject(), "route policies should be object");
    std::map<std::string, folly::dynamic> items;
    for (const auto& it : jpolicies.items()) {
      checkLogic(it.first.isString(), "route key should be string");
      auto key = it.first.asString().toStdString();
      items.insert(std::make_pair(key, it.second));
    }
    // order is important
    for (const auto& it : items) {
      policies.emplace(it.first, factory.createRoot(it.second));
    }
  }

  if (json.count("wildcard")) {
    wildcard = factory.createRoot(json["wildcard"]);
  }

  checkLogic(json.count("wildcard") || json.count("policies"), "Empty route");
}

}}}  // facebook::memcache::mcrouter
