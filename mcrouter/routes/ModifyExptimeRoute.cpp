/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ModifyExptimeRoute.h"

#include <folly/Range.h>
#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace {

ModifyExptimeAction stringToAction(folly::StringPiece s) {
  if (s == "set") {
    return ModifyExptimeAction::Set;
  } else if (s == "min") {
    return ModifyExptimeAction::Min;
  }
  checkLogic(false, "ModifyExptimeRoute: action should be 'set' or 'min'");
  return ModifyExptimeAction::Set;
}

} // anonymous namespace

const char* actionToString(ModifyExptimeAction action) {
  switch (action) {
    case ModifyExptimeAction::Set:
      return "set";
    case ModifyExptimeAction::Min:
      return "min";
  }
  assert(false);
  return "";
}

McrouterRouteHandlePtr makeModifyExptimeRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "ModifyExptimeRoute: should be an object");
  auto jtarget = json.get_ptr("target");
  checkLogic(jtarget, "ModifyExptimeRoute: no target");
  auto target = factory.create(*jtarget);
  auto jexptime = json.get_ptr("exptime");
  checkLogic(jexptime, "ModifyExptimeRoute: no exptime");
  checkLogic(jexptime->isInt(), "ModifyExptimeRoute: exptime is not an int");
  auto exptime = jexptime->getInt();

  ModifyExptimeAction action{ModifyExptimeAction::Set};
  if (auto jaction = json.get_ptr("action")) {
    checkLogic(
        jaction->isString(), "ModifyExptimeRoute: action is not a string");
    action = stringToAction(jaction->getString());
  }

  // 0 means infinite exptime
  if (action == ModifyExptimeAction::Min && exptime == 0) {
    return target;
  }

  return makeMcrouterRouteHandle<ModifyExptimeRoute>(
      std::move(target), exptime, action);
}
}
}
} // facebook::memcache::mcrouter
