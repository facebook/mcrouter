/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/routes/MigrateRoute.h"
#include "mcrouter/routes/TimeProviderFunc.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeMigrateRoute(
    typename RouterInfo::RouteHandlePtr fh,
    typename RouterInfo::RouteHandlePtr th,
    time_t start_time_sec,
    time_t interval_sec) {
  return makeRouteHandle<
      typename RouterInfo::RouteHandleIf,
      MigrateRoute,
      TimeProviderFunc>(
      std::move(fh),
      std::move(th),
      start_time_sec,
      interval_sec,
      TimeProviderFunc());
}

} // detail

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeMigrateRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "MigrateRoute should be object");
  checkLogic(
      json.count("start_time") && json["start_time"].isInt(),
      "MigrateRoute has no/invalid start_time");
  checkLogic(json.count("from"), "MigrateRoute has no 'from' route");
  checkLogic(json.count("to"), "MigrateRoute has no 'to' route");

  time_t startTimeSec = json["start_time"].getInt();
  time_t intervalSec = 3600;
  if (auto jinterval = json.get_ptr("interval")) {
    checkLogic(jinterval->isInt(), "MigrateRoute interval is not integer");
    intervalSec = jinterval->asInt();
  }

  return detail::makeMigrateRoute<RouterInfo>(
      factory.create(json["from"]),
      factory.create(json["to"]),
      startTimeSec,
      intervalSec);
}
} // mcrouter
} // memcache
} // facebook
