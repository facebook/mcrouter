/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "PrefixSelectorRoute.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <folly/dynamic.h>
#include <folly/Range.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache { namespace mcrouter {

PrefixSelectorRoute::PrefixSelectorRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  // if json is not PrefixSelectorRoute, just treat it as a wildcard.
  if (!json.isObject() || !json.count("type") || !json["type"].isString() ||
      json["type"].stringPiece() != "PrefixSelectorRoute") {
    wildcard = factory.create(json);
    return;
  }

  auto jPolicies = json.get_ptr("policies");
  auto jWildcard = json.get_ptr("wildcard");
  checkLogic(jPolicies || jWildcard,
             "PrefixSelectorRoute: no policies/wildcard");
  if (jPolicies) {
    checkLogic(jPolicies->isObject(),
               "PrefixSelectorRoute: policies is not an object");
    std::vector<std::pair<folly::StringPiece, const folly::dynamic*>> items;
    for (const auto& it : jPolicies->items()) {
      checkLogic(it.first.isString(),
                 "PrefixSelectorRoute: policy key is not a string");
      items.emplace_back(it.first.stringPiece(), &it.second);
    }
    // order is important
    std::sort(items.begin(), items.end());
    for (const auto& it : items) {
      policies.emplace(it.first, factory.create(*it.second));
    }
  }

  if (jWildcard) {
    wildcard = factory.create(*jWildcard);
  }
}

}}}  // facebook::memcache::mcrouter
