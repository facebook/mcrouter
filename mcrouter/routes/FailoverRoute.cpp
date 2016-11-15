/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FailoverRoute.h"

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/ExtraRouteHandleProviderIf.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeNullRoute();

McrouterRouteHandlePtr
makeNullOrSingletonRoute(std::vector<McrouterRouteHandlePtr> rh) {
  assert(rh.size() <= 1);
  if (rh.empty()) {
    return makeNullRoute();
  }
  return std::move(rh[0]);
}

McrouterRouteHandlePtr makeFailoverRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json,
    ExtraRouteHandleProviderIf& extraProvider) {
  std::vector<McrouterRouteHandlePtr> children;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
  } else {
    children = factory.createList(json);
  }
  return extraProvider.makeFailoverRoute(json, std::move(children));
}

}}}  // facebook::memcache::mcrouter
