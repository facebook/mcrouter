/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "DevNullRoute.h"

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace folly {
struct dynamic;
} // folly

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeDevNullRoute() {
  return makeMcrouterRouteHandleWithInfo<DevNullRoute>();
}

McrouterRouteHandlePtr makeDevNullRoute(
    RouteHandleFactory<McrouterRouteHandleIf>&,
    const folly::dynamic&) {
  return makeDevNullRoute();
}

}}}  // facebook::memcache::mcrouter
