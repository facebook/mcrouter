/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ModifyExptimeRoute.h"

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeModifyExptimeRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  checkLogic(json.isObject(), "ModifyExptimeRoute: should be an object");
  auto jtarget = json.get_ptr("target");
  checkLogic(jtarget, "ModifyExptimeRoute: no target");
  auto jexptime = json.get_ptr("exptime");
  checkLogic(jexptime, "ModifyExptimeRoute: no exptime");
  checkLogic(jexptime->isInt(), "ModifyExptimeRoute: exptime is not an int");

  return std::make_shared<McrouterRouteHandle<ModifyExptimeRoute>>(
    factory.create(*jtarget),
    jexptime->getInt());
}

}}}  // facebook::memcache::mcrouter
