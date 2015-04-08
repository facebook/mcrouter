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

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeRateLimitRoute(
  McrouterRouteHandlePtr normalRoute,
  RateLimiter rateLimiter) {

  return std::make_shared<McrouterRouteHandle<RateLimitRoute>>(
    std::move(normalRoute),
    std::move(rateLimiter));
}

}}}  // facebook::memcache::mcrouter
