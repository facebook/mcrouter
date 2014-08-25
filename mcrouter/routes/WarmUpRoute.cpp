/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/lib/routes/WarmUpRoute.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeWarmUpRouteAdd(
  McrouterRouteHandlePtr warmh,
  McrouterRouteHandlePtr coldh,
  uint32_t exptime,
  size_t ncacheExptime,
  size_t ncacheUpdatePeriod) {

  return makeMcrouterRouteHandle<WarmUpRoute, McOperation<mc_op_add>>(
    std::move(warmh),
    std::move(coldh),
    exptime,
    ncacheExptime,
    ncacheUpdatePeriod);
}

McrouterRouteHandlePtr makeWarmUpRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json,
  uint32_t exptime) {

  return makeMcrouterRouteHandle<WarmUpRoute, McOperation<mc_op_add>>(
    factory, json, exptime);
}

}}}
