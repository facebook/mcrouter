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

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache { namespace mcrouter {

OperationSelectorRoute::OperationSelectorRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  if (!json.isObject()) {
    defaultPolicy_ = factory.create(json);
    return;
  }

  if (json.count("default_policy")) {
    defaultPolicy_ = factory.create(json["default_policy"]);
  }

  operationPolicies_.resize(mc_nops);
  if (json.count("operation_policies")) {
    const auto& policies = json["operation_policies"];
    checkLogic(policies.isObject(),
               "OperationSelectorRoute: operation_policies is not object");

    std::map<std::string, folly::dynamic> orderedPolicies;
    for (const auto& it : policies.items()) {
      checkLogic(it.first.isString(),
                 "OperationSelectorRoute: operation_policies "
                 "key is not a string");
      auto key = it.first.asString().toStdString();
      orderedPolicies.insert({ key, it.second });
    }

    // order is important here: named handles may not be resolved if we parse
    // policies in random order
    for (const auto& it : orderedPolicies) {
      auto opId = mc_op_from_string(it.first.data());
      checkLogic(opId != mc_op_unknown, "Unknown mc operation: {}", it.first);
      operationPolicies_[opId] = factory.create(it.second);
    }
  }
}

McrouterRouteHandlePtr makeOperationSelectorRoute(
  std::string name,
  std::vector<McrouterRouteHandlePtr> operationPolicies,
  McrouterRouteHandlePtr defaultPolicy) {

  return std::make_shared<McrouterRouteHandle<OperationSelectorRoute>>(
    std::move(name),
    std::move(operationPolicies),
    std::move(defaultPolicy));
}

McrouterRouteHandlePtr makeOperationSelectorRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  return std::make_shared<McrouterRouteHandle<OperationSelectorRoute>>(
    factory, json);
}

}}}  // facebook::memcache::mcrouter
