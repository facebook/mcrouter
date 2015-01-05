/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/routes/MigrateRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/TimeProviderFunc.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeMigrateRoute(
  McrouterRouteHandlePtr fh,
  McrouterRouteHandlePtr th,
  time_t start_time_sec,
  time_t interval_sec) {

  return makeMcrouterRouteHandle<MigrateRoute, TimeProviderFunc>(
    std::move(fh),
    std::move(th),
    start_time_sec,
    interval_sec,
    TimeProviderFunc());
}

McrouterRouteHandlePtr makeMigrateRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  return makeMcrouterRouteHandle<MigrateRoute, TimeProviderFunc>(
    factory, json, TimeProviderFunc());
}

}}}
