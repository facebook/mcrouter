/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "LoggingRoute.h"

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeLoggingRoute(McrouterRouteHandlePtr rh) {
  return std::make_shared<McrouterRouteHandle<LoggingRoute>>(std::move(rh));
}

McrouterRouteHandlePtr makeLoggingRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  McrouterRouteHandlePtr target;
  if (json.isObject()) {
    if (auto jtarget = json.get_ptr("target")) {
      target = factory.create(*jtarget);
    }
  } else if (json.isString()) {
    target = factory.create(json);
  }
  return makeLoggingRoute(std::move(target));
}

}}}  // facebook::memcache::mcrouter
