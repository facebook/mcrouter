/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/carbon/RequestReplyUtil.h"

namespace folly {
struct dynamic;
}

namespace facebook {
namespace memcache {

template <class RouteHandleIf>
class RouteHandleFactory;

namespace mcrouter {

/* RouteHandle that can send to a different target based on McOperation id */
template <class RouterInfo>
class OperationSelectorRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;
  using RouteHandlePtr = typename RouterInfo::RouteHandlePtr;
  using RoutableRequests = typename RouterInfo::RoutableRequests;

 public:
  static std::string routeName() {
    return "operation-selector";
  }

  OperationSelectorRoute(
      carbon::RequestIdMap<RoutableRequests, RouteHandlePtr> operationPolicies,
      RouteHandlePtr&& defaultPolicy)
      : operationPolicies_(std::move(operationPolicies)),
        defaultPolicy_(std::move(defaultPolicy)) {}

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    if (const auto& rh =
            operationPolicies_.template getByRequestType<Request>()) {
      t(*rh, req);
    } else if (defaultPolicy_) {
      t(*defaultPolicy_, req);
    }
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    if (const auto& rh =
            operationPolicies_.template getByRequestType<Request>()) {
      return rh->route(req);
    } else if (defaultPolicy_) {
      return defaultPolicy_->route(req);
    }

    return ReplyT<Request>();
  }

 private:
  const carbon::RequestIdMap<RoutableRequests, RouteHandlePtr>
      operationPolicies_;
  const RouteHandlePtr defaultPolicy_;
};

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeOperationSelectorRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json);

} // mcrouter
} // memcache
} // facebook

#include "OperationSelectorRoute-inl.h"
