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
#include "mcrouter/lib/FailoverRecorder.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/McrouterFiberContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

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
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(targets_, req, Operation());
  }

  FailoverRoute() = default;

  FailoverRoute(std::vector<std::shared_ptr<RouteHandleIf>> targets,
                FailoverErrorsSettings failoverErrors)
      : targets_(std::move(targets)),
        failoverErrors_(std::move(failoverErrors)) {
  }

  FailoverRoute(std::vector<std::shared_ptr<RouteHandleIf>> targets,
                FailoverErrorsSettings failoverErrors,
                FailoverRecorder failoverRecorder)
      : targets_(std::move(targets)),
        failoverErrors_(std::move(failoverErrors)),
        failoverRecorder_(std::move(failoverRecorder)) {
  }


  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    if (targets_.empty()) {
      return NullRoute<RouteHandleIf>::route(req, Operation());
    }

    return fiber_local::runWithLocals([this, &req]() {
      for (size_t i = 0; i + 1 < targets_.size(); ++i) {
        auto delay = failoverErrors_.failbackDelay(Operation());
        if (delay) {
          const time_t lastFailover = failoverRecorder_.getLastFailover(i, Operation());
          time_t now;
          time(&now);
          if (difftime(now, lastFailover) < delay) {
            continue;
          }
        }
        fiber_local::setRequestClass(RequestClass::FAILOVER);
        auto reply = targets_[i]->route(req, Operation());
        if (failoverErrors_.shouldFailover(reply, Operation())) {
          if (delay) {
            failoverRecorder_.setLastFailover(i, Operation());
          }
        } else {
          return reply;
        }
      }
      fiber_local::setRequestClass(RequestClass::FAILOVER);
      return targets_.back()->route(req, Operation());
    });
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> targets_;
  const FailoverErrorsSettings failoverErrors_;
  mutable FailoverRecorder failoverRecorder_;
};

}}} // facebook::memcache::mcrouter
