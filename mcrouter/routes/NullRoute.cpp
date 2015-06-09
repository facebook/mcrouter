/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeNullRoute() {
  return makeMcrouterRouteHandle<NullRoute>();
}

McrouterRouteHandlePtr makeNullRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  return makeNullRoute();
}

}}}  // facebook::memcache::mcrouter
