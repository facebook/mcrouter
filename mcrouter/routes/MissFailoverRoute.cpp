/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "MissFailoverRoute.h"

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeNullRoute();

McrouterRouteHandlePtr makeMissFailoverRoute(
  std::vector<McrouterRouteHandlePtr> targets) {

  if (targets.empty()) {
    return makeNullRoute();
  }

  if (targets.size() == 1) {
    return std::move(targets[0]);
  }

  return makeMcrouterRouteHandle<MissFailoverRoute>(std::move(targets));
}

McrouterRouteHandlePtr makeMissFailoverRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  std::vector<McrouterRouteHandlePtr> children;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
  } else {
    children = factory.createList(json);
  }
  return makeMissFailoverRoute(std::move(children));
}

}}}  // facebook::memcache::mcrouter
