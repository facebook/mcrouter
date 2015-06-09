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
#include "mcrouter/lib/routes/ErrorRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeErrorRoute(std::string valueToSet = "") {
  return makeMcrouterRouteHandle<ErrorRoute>(std::move(valueToSet));
}

McrouterRouteHandlePtr makeErrorRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isString() || json.isNull(),
             "ErrorRoute: response should be string or empty");
  std::string response;
  if (json.isString()) {
    response = json.stringPiece().str();
  }
  return makeErrorRoute(std::move(response));
}

}}}  // facebook::memcache::mcrouter
