/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "OperationSelectorRoute.h"

#include <map>

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeOperationSelectorRoute(
  std::vector<McrouterRouteHandlePtr> operationPolicies,
  McrouterRouteHandlePtr defaultPolicy) {

  return std::make_shared<McrouterRouteHandle<OperationSelectorRoute>>(
    std::move(operationPolicies),
    std::move(defaultPolicy));
}

McrouterRouteHandlePtr makeOperationSelectorRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  if (!json.isObject()) {
    return factory.create(json);
  }

  McrouterRouteHandlePtr defaultPolicy;
  if (json.count("default_policy")) {
    defaultPolicy = factory.create(json["default_policy"]);
  }

  std::vector<McrouterRouteHandlePtr> operationPolicies{mc_nops};
  if (auto jOpPolicies = json.get_ptr("operation_policies")) {
    checkLogic(jOpPolicies->isObject(),
               "OperationSelectorRoute: operation_policies is not an object");

    std::map<std::string, const folly::dynamic*> orderedPolicies;
    for (auto& it : jOpPolicies->items()) {
      checkLogic(it.first.isString(),
                 "OperationSelectorRoute: operation_policies' "
                 "key is not a string");
      auto key = it.first.stringPiece().str();
      orderedPolicies.emplace(std::move(key), &it.second);
    }

    // order is important here: named handles may not be resolved if we parse
    // policies in random order
    for (const auto& it : orderedPolicies) {
      auto opId = mc_op_from_string(it.first.data());
      checkLogic(opId != mc_op_unknown, "Unknown mc operation: {}", it.first);
      operationPolicies[opId] = factory.create(*it.second);
    }

    return makeOperationSelectorRoute(std::move(operationPolicies),
                                      std::move(defaultPolicy));
  }

  return defaultPolicy;
}

}}}  // facebook::memcache::mcrouter
