/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
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
  // if json is not PrefixRouteSelector, just treat it as RouteHandle
  // for wildcard.
  // NOTE: "route" is deprecated and will go away soon
  if (!json.isObject() || !json.count("type") || !json["type"].isString() ||
      (json["type"].asString() != "route" &&
       json["type"].asString() != "PrefixSelectorRoute")) {
    wildcard = factory.create(json);
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
      policies.emplace(it.first, factory.create(it.second));
    }
  }

  if (json.count("wildcard")) {
    wildcard = factory.create(json["wildcard"]);
  }

  checkLogic(json.count("wildcard") || json.count("policies"), "Empty route");
}

}}}  // facebook::memcache::mcrouter
