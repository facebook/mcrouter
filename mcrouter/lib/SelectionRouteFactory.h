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

#include <vector>

#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/routes/SelectionRoute.h"
#include "mcrouter/routes/ErrorRoute.h"

namespace facebook {
namespace memcache {

/**
 * Constructs a SelectionRoute for the given "RouterInfo".
 *
 * @param children                List of children route handles.
 * @param selector                Selector responsible for choosing to which
 *                                of the children the request should be sent
 *                                to.
 * @param outOfRangeDestination   The destination to which the request will be
 *                                routed if selector.select() returns a value
 *                                that is >= than children.size().
 */
template <class RouterInfo, class Selector>
typename RouterInfo::RouteHandlePtr createSelectionRoute(
    std::vector<typename RouterInfo::RouteHandlePtr> children,
    Selector selector,
    typename RouterInfo::RouteHandlePtr outOfRangeDestination = nullptr) {
  if (!outOfRangeDestination) {
    outOfRangeDestination =
        mcrouter::createErrorRoute<RouterInfo>("Invalid destination index.");
  }
  if (children.empty()) {
    return std::move(outOfRangeDestination);
  }
  return makeRouteHandleWithInfo<RouterInfo, SelectionRoute, Selector>(
      std::move(children),
      std::move(selector),
      std::move(outOfRangeDestination));
}

} // memcache
} // facebook
