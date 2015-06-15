/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "RateLimitRoute.h"

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeRateLimitRoute(McrouterRouteHandlePtr normalRoute,
                                          RateLimiter rateLimiter) {
  return std::make_shared<McrouterRouteHandle<RateLimitRoute>>(
    std::move(normalRoute),
    std::move(rateLimiter));
}

McrouterRouteHandlePtr makeRateLimitRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "RateLimitRoute is not an object");
  auto jtarget = json.get_ptr("target");
  checkLogic(jtarget, "RateLimitRoute: target not found");
  auto target = factory.create(*jtarget);
  auto jrates = json.get_ptr("rates");
  checkLogic(jrates, "RateLimitRoute: rates not found");
  return makeRateLimitRoute(std::move(target), RateLimiter(*jrates));
}

}}}  // facebook::memcache::mcrouter
