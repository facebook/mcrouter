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
#include "mcrouter/route.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/RouteHandleMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

/* RouteHandle that can send to a different target based on McOperation id */
class OperationSelectorRoute {
 public:
  using ContextPtr = std::shared_ptr<ProxyRequestContext>;

  static std::string routeName() { return "operation-selector"; }

  OperationSelectorRoute(
    std::vector<McrouterRouteHandlePtr> operationPolicies,
    McrouterRouteHandlePtr&& defaultPolicy)
      : operationPolicies_(std::move(operationPolicies)),
        defaultPolicy_(std::move(defaultPolicy)) {
  }

  OperationSelectorRoute(RouteHandleFactory<McrouterRouteHandleIf>& factory,
                         const folly::dynamic& json);

  template <int M, class Request>
  std::vector<McrouterRouteHandlePtr> couldRouteTo(
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
    const Request& req, McOperation<M>, const ContextPtr& ctx) const {

    if (operationPolicies_[M]) {
      return operationPolicies_[M]->route(req, McOperation<M>(), ctx);
    } else if (defaultPolicy_) {
      return defaultPolicy_->route(req, McOperation<M>(), ctx);
    }

    using Reply = typename ReplyType<McOperation<M>, Request>::type;
    return Reply(DefaultReply, McOperation<M>());
  }

private:
  std::vector<McrouterRouteHandlePtr> operationPolicies_;
  McrouterRouteHandlePtr defaultPolicy_;
};

}}}  // facebook::memcache::mcrouter
