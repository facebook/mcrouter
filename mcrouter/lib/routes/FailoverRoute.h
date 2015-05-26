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

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache {

/**
 * Sends the same request sequentially to each destination in the list in order,
 * until the first non-error reply.  If all replies result in errors, returns
 * the last destination's reply.
 */
template <class RouteHandleIf>
class FailoverRoute {
 public:
  static std::string routeName() { return "failover"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    return targets_;
  }

  FailoverRoute() = default;

  explicit FailoverRoute(std::vector<std::shared_ptr<RouteHandleIf>> targets,
              FailoverErrorsSettings failoverErrors = FailoverErrorsSettings())
      : targets_(std::move(targets)),
        failoverErrors_(std::move(failoverErrors)) {
  }

  FailoverRoute(RouteHandleFactory<RouteHandleIf>& factory,
                const folly::dynamic& json) {
    if (json.isObject()) {
      if (json.count("children")) {
        targets_ = factory.createList(json["children"]);
      }
      if (auto jsonFailoverErrors = json.get_ptr("failover_errors")) {
        failoverErrors_ = FailoverErrorsSettings(*jsonFailoverErrors);
      }
    } else {
      targets_ = factory.createList(json);
    }
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    if (targets_.empty()) {
      return NullRoute<RouteHandleIf>::route(req, Operation());
    }

    for (size_t i = 0; i + 1 < targets_.size(); ++i) {
      auto reply = targets_[i]->route(req, Operation());
      if (!failoverErrors_.shouldFailover(reply, Operation())) {
        return reply;
      }
    }

    return targets_.back()->route(req, Operation());
  }

 private:
  std::vector<std::shared_ptr<RouteHandleIf>> targets_;
  FailoverErrorsSettings failoverErrors_;
};

}}
