/*
 *  Copyright (c) 2016, Facebook, Inc.
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
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr createLoggingRoute(McrouterRouteHandlePtr rh) {
  return makeMcrouterRouteHandleWithInfo<LoggingRoute>(std::move(rh));
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
  return createLoggingRoute(std::move(target));
}

}}}  // facebook::memcache::mcrouter
