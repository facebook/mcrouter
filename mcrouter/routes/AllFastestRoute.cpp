/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/routes/AllFastestRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache {

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, AllFastestRoute>(
  RouteHandleFactory<mcrouter::McrouterRouteHandleIf>&,
  const folly::dynamic&);

namespace mcrouter {

McrouterRouteHandlePtr makeAllFastestRoute(
  std::vector<McrouterRouteHandlePtr> rh) {

  return makeMcrouterRouteHandle<AllFastestRoute>(
    std::move(rh));
}

}}}
