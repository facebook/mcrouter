/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "RateLimitRoute.h"

#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeRateLimitRoute(
  McrouterRouteHandlePtr normalRoute,
  RateLimiter rateLimiter) {

  return makeMcrouterRouteHandle<RateLimitRoute>(
    std::move(normalRoute),
    std::move(rateLimiter));
}

}}}  // facebook::memcache::mcrouter
