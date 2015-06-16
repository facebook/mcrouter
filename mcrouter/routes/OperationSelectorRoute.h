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

#include <memory>
#include <string>
#include <vector>

#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/* RouteHandle that can send to a different target based on McOperation id */
class OperationSelectorRoute {
 public:
  static std::string routeName() { return "operation-selector"; }

  OperationSelectorRoute(
    std::vector<McrouterRouteHandlePtr> operationPolicies,
    McrouterRouteHandlePtr&& defaultPolicy)
      : operationPolicies_(std::move(operationPolicies)),
        defaultPolicy_(std::move(defaultPolicy)) {
  }

  template <int M, class Request>
  void traverse(const Request& req, McOperation<M>,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    if (operationPolicies_[M]) {
      t(*operationPolicies_[M], req, McOperation<M>());
    } else if (defaultPolicy_) {
      t(*defaultPolicy_, req, McOperation<M>());
    }
  }

  template<int M, class Request>
  typename ReplyType<McOperation<M>, Request>::type route(
    const Request& req, McOperation<M>) const {

    if (operationPolicies_[M]) {
      return operationPolicies_[M]->route(req, McOperation<M>());
    } else if (defaultPolicy_) {
      return defaultPolicy_->route(req, McOperation<M>());
    }

    using Reply = typename ReplyType<McOperation<M>, Request>::type;
    return Reply(DefaultReply, McOperation<M>());
  }

private:
  const std::vector<McrouterRouteHandlePtr> operationPolicies_;
  const McrouterRouteHandlePtr defaultPolicy_;
};

}}}  // facebook::memcache::mcrouter
