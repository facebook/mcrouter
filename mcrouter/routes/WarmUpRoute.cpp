/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "WarmUpRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeWarmUpRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  return makeMcrouterRouteHandle<WarmUpRoute>(factory, json);
}

McrouterRouteHandlePtr makeWarmUpRoute(
  McrouterRouteHandlePtr warm,
  McrouterRouteHandlePtr cold,
  uint32_t exptime) {
  return makeMcrouterRouteHandle<WarmUpRoute>(
    std::move(warm), std::move(cold), exptime);
}

}}}
