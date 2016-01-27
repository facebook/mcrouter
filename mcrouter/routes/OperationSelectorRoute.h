/*
 *  Copyright (c) 2016, Facebook, Inc.
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

#include "mcrouter/lib/McRequest.h"
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

  template <int M>
  void traverse(const McRequestWithMcOp<M>& req,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    if (operationPolicies_[M]) {
      t(*operationPolicies_[M], req);
    } else if (defaultPolicy_) {
      t(*defaultPolicy_, req);
    }
  }

  template <int M>
  McReply route(const McRequestWithMcOp<M>& req) const {
    if (operationPolicies_[M]) {
      return operationPolicies_[M]->route(req);
    } else if (defaultPolicy_) {
      return defaultPolicy_->route(req);
    }

    return McReply(DefaultReply, req);
  }

private:
  const std::vector<McrouterRouteHandlePtr> operationPolicies_;
  const McrouterRouteHandlePtr defaultPolicy_;
};

}}}  // facebook::memcache::mcrouter
