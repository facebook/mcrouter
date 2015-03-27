/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/RouteHandleMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

/* RouteHandle that can send to a different target based on McOperation id */
template <class RouteHandleIf>
class OperationSelectorRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;
  using StackContext = typename RouteHandleIf::StackContext;

  static std::string routeName() { return "operation-selector"; }

  OperationSelectorRoute(
    std::vector<std::shared_ptr<RouteHandleIf>> operationPolicies,
    std::shared_ptr<RouteHandleIf>&& defaultPolicy)
      : operationPolicies_(std::move(operationPolicies)),
        defaultPolicy_(std::move(defaultPolicy)) {
  }

   OperationSelectorRoute(RouteHandleFactory<RouteHandleIf>& factory,
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

  template <int M, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, McOperation<M>, const ContextPtr& ctx) const {

    if (operationPolicies_[M]) {
      return {operationPolicies_[M]};
    } else if (defaultPolicy_) {
      return {defaultPolicy_};
    }

    return {};
  }

  template<int M, class Request>
  typename ReplyType<McOperation<M>, Request>::type route(
    const Request& req, McOperation<M>, const ContextPtr& ctx,
    StackContext&& sctx) const {

    if (operationPolicies_[M]) {
      return operationPolicies_[M]->route(req, McOperation<M>(), ctx,
                                          std::move(sctx));
    } else if (defaultPolicy_) {
      return defaultPolicy_->route(req, McOperation<M>(), ctx, std::move(sctx));
    }

    return NullRoute<RouteHandleIf>::route(req, McOperation<M>(), ctx,
                                           std::move(sctx));
  }

private:
  std::vector<std::shared_ptr<RouteHandleIf>> operationPolicies_;
  std::shared_ptr<RouteHandleIf> defaultPolicy_;
};

}}}  // facebook::memcache::mcrouter
