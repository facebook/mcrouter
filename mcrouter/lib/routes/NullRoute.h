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
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/config/RouteHandleBuilder.h"

namespace folly {
struct dynamic;
}

namespace facebook {
namespace memcache {

template <class RouteHandleIf>
class RouteHandleFactory;

/**
 * Returns the default reply for each request right away
 */
template <class RouteHandleIf>
struct NullRoute {
  static std::string routeName() {
    return "null";
  }

  template <class Request>
  void traverse(const Request&, const RouteHandleTraverser<RouteHandleIf>&)
      const {}

  template <class Request>
  static ReplyT<Request> route(const Request& req) {
    return createReply(DefaultReply, req);
  }
};

namespace mcrouter {

template <class RouteHandleIf>
std::shared_ptr<RouteHandleIf> createNullRoute() {
  return makeRouteHandle<RouteHandleIf, NullRoute>();
}

template <class RouteHandleIf>
std::shared_ptr<RouteHandleIf> makeNullRoute(
    RouteHandleFactory<RouteHandleIf>&,
    const folly::dynamic&) {
  return createNullRoute<RouteHandleIf>();
}

template <class RouteHandleIf>
std::shared_ptr<RouteHandleIf> makeNullOrSingletonRoute(
    std::vector<std::shared_ptr<RouteHandleIf>> rh) {
  assert(rh.size() <= 1);
  if (rh.empty()) {
    return createNullRoute<RouteHandleIf>();
  }
  return std::move(rh[0]);
}

} // mcrouter
}
} // facebook::memcache
