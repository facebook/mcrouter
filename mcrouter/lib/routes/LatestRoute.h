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

#include <folly/Hash.h>

#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/routes/FailoverRoute.h"

namespace facebook { namespace memcache {

/**
 * Connection selector route that attempts to "behave well" in how many
 * new targets it connects to.
 *
 * Creates a FailoverRoute with at most failoverCount destinations chosen
 * pseudo-randomly based on hostid.
 */
template <class RouteHandleIf>
class LatestRoute {
 public:
  static std::string routeName() { return "latest"; }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    route_.traverse(req, Operation(), t);
  }

  LatestRoute(std::vector<std::shared_ptr<RouteHandleIf>> targets,
              size_t failoverCount,
              FailoverErrorsSettings failoverErrors)
    : route_(getFailoverTargets(std::move(targets), failoverCount),
             std::move(failoverErrors)) {
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type
  route(const Request& req, Operation) {
    return route_.route(req, Operation());
  }

 private:
  const FailoverRoute<RouteHandleIf> route_;

  static std::vector<std::shared_ptr<RouteHandleIf>> getFailoverTargets(
      std::vector<std::shared_ptr<RouteHandleIf>> targets,
      size_t failoverCount) {
    std::vector<std::shared_ptr<RouteHandleIf>> failovers;
    failoverCount = std::min(failoverCount, targets.size());
    size_t curHash = folly::hash::hash_combine(0, globals::hostid());
    for (size_t i = 0; i < failoverCount; ++i) {
      auto id = curHash % targets.size();
      failovers.push_back(std::move(targets[id]));
      std::swap(targets[id], targets[targets.size() - 1]);
      targets.pop_back();
      curHash = folly::hash::hash_combine(curHash, i);
    }
    return failovers;
  }
};

}}  // facebook::memcache
