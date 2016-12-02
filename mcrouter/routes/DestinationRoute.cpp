/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "DestinationRoute.h"

#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeDestinationRoute(
    std::shared_ptr<ProxyDestination> destination,
    std::string poolName,
    size_t indexInPool,
    std::chrono::milliseconds timeout,
    bool keepRoutingPrefix) {
  return makeMcrouterRouteHandleWithInfo<DestinationRoute>(
      std::move(destination),
      std::move(poolName),
      indexInPool,
      timeout,
      keepRoutingPrefix);
}

}}}  // facebook::memcache::mcrouter
