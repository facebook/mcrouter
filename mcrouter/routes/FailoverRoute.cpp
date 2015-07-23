/*
 *  Copyright (c) 2015, Facebook, Inc.
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
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeNullRoute();

McrouterRouteHandlePtr makeFailoverRoute(
  std::vector<McrouterRouteHandlePtr> rh,
  FailoverErrorsSettings failoverErrors) {

  if (rh.empty()) {
    return makeNullRoute();
  }

  if (rh.size() == 1) {
    return std::move(rh[0]);
  }

  FailoverRecorder failoverRecorder(rh.size());;

  return makeMcrouterRouteHandle<FailoverRoute>(std::move(rh),
                                                std::move(failoverErrors),
                                                std::move(failoverRecorder));
}

McrouterRouteHandlePtr makeFailoverRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  std::vector<McrouterRouteHandlePtr> children;
  FailoverErrorsSettings failoverErrors;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
    if (auto jFailoverErrors = json.get_ptr("failover_errors")) {
      failoverErrors = FailoverErrorsSettings(*jFailoverErrors);
    }
  } else {
    children = factory.createList(json);
  }
  return makeFailoverRoute(std::move(children), std::move(failoverErrors));
}

}}}  // facebook::memcache::mcrouter
