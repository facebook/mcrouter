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
#include <folly/Range.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

ModifyExptimeRoute::Action stringToAction(folly::StringPiece s) {
  if (s == "set") {
    return ModifyExptimeRoute::Action::Set;
  } else if (s == "min") {
    return ModifyExptimeRoute::Action::Min;
  }
  checkLogic(false, "ModifyExptimeRoute: action should be 'set' or 'min'");
  return ModifyExptimeRoute::Action::Set;
}

} // anonymous

const char* ModifyExptimeRoute::actionToString(Action action) {
  switch (action) {
    case Action::Set: return "set";
    case Action::Min: return "min";
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

  ModifyExptimeRoute::Action action{ModifyExptimeRoute::Action::Set};
  if (auto jaction = json.get_ptr("action")) {
    checkLogic(jaction->isString(),
               "ModifyExptimeRoute: action is not a string");
    action = stringToAction(jaction->getString());
  }

  // 0 means infinite exptime
  if (action == ModifyExptimeRoute::Action::Min && exptime == 0) {
    return target;
  }

  return std::make_shared<McrouterRouteHandle<ModifyExptimeRoute>>(
    std::move(target), exptime, action);
}

}}}  // facebook::memcache::mcrouter
