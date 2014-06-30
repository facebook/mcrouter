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
#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/routes/LatestRoute.h"

namespace facebook { namespace memcache {

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, LatestRoute>(
  const folly::dynamic&,
  std::vector<std::shared_ptr<mcrouter::McrouterRouteHandleIf>>&&);

namespace mcrouter {

McrouterRouteHandlePtr makeLatestRoute(
  std::string name,
  std::vector<McrouterRouteHandlePtr> targets,
  size_t failoverCount) {

  return makeMcrouterRouteHandle<LatestRoute>(
    std::move(name),
    std::move(targets),
    failoverCount);
}

}}}
