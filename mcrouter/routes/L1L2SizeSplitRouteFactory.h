/*
 *  Copyright (c) 2017, Facebook, Inc.
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
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/routes/L1L2SizeSplitRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeL1L2SizeSplitRoute(
    typename RouterInfo::RouteHandlePtr l1,
    typename RouterInfo::RouteHandlePtr l2,
    size_t threshold,
    bool bothFullSet) {
  return makeRouteHandle<
      typename RouterInfo::RouteHandleIf,
      L1L2SizeSplitRoute>(std::move(l1), std::move(l2), threshold, bothFullSet);
}

} // namespace detail

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeL1L2SizeSplitRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "L1L2SizeSplitRoute should be an object");
  checkLogic(json.count("l1"), "L1L2SizeSplitRoute: no l1 route");
  checkLogic(json.count("l2"), "L1L2SizeSplitRoute: no l2 route");
  checkLogic(json.count("threshold"), "L1L2SizeSplitRoute: no threshold");
  checkLogic(
      json["threshold"].isInt(),
      "L1L2SizeSplitRoute: threshold is not an integer");
  size_t threshold = json["threshold"].getInt();

  bool bothFullSet = false;
  if (json.count("bothFullSet")) {
    checkLogic(
        json["bothFullSet"].isBool(),
        "L1L2SizeSplitRoute: bothFullSet is not an boolean");
    bothFullSet = json["bothFullSet"].getBool();
  }

  return detail::makeL1L2SizeSplitRoute<RouterInfo>(
      factory.create(json["l1"]),
      factory.create(json["l2"]),
      threshold,
      bothFullSet);
}
} // namespace mcrouter
} // namespace memcache
} // namespace facebook
