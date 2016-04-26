/*
 *  Copyright (c) 2016, Facebook, Inc.
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

McrouterRouteHandlePtr makeErrorRoute(std::string valueToSet) {
  return makeMcrouterRouteHandle<ErrorRoute>(std::move(valueToSet));
}

McrouterRouteHandlePtr makeErrorRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject() || json.isString() || json.isNull(),
             "ErrorRoute: should be string or object");
  std::string response;
  if (json.isString()) {
    response = json.getString();
  } else if (json.isObject()) {
    if (auto jResponse = json.get_ptr("response")) {
      checkLogic(jResponse->isString(), "ErrorRoute: response is not a string");
      response = jResponse->getString();
    }
  }
  return makeErrorRoute(std::move(response));
}

}}}  // facebook::memcache::mcrouter
