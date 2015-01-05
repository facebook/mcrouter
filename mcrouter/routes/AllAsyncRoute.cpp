/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/routes/AllAsyncRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache {

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, AllAsyncRoute>(
  RouteHandleFactory<mcrouter::McrouterRouteHandleIf>&,
  const folly::dynamic&);

namespace mcrouter {

McrouterRouteHandlePtr makeAllAsyncRoute(
  std::vector<McrouterRouteHandlePtr> rh) {

  return makeMcrouterRouteHandle<AllAsyncRoute>(
    std::move(rh));
}

}}}
