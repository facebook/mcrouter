/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AsynclogRoute.h"

#include <utility>

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeAsynclogRoute(McrouterRouteHandlePtr rh,
                                         std::string asynclogName) {
  return std::make_shared<McrouterRouteHandle<AsynclogRoute>>(
    std::move(rh),
    std::move(asynclogName));
}

/**
 * @return target and asynclogName
 *         Caller may call makeAsynclogRoute afterwards.
 */
std::pair<McrouterRouteHandlePtr, std::string> parseAsynclogRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  std::string asynclogName;
  McrouterRouteHandlePtr target;
  checkLogic(json.isObject() || json.isString(),
             "AsynclogRoute should be object or string");
  if (json.isString()) {
    asynclogName = json.stringPiece().str();
    target = factory.create(json);
  } else { // object
    auto jname = json.get_ptr("name");
    checkLogic(jname && jname->isString(),
               "AsynclogRoute: required string name");
    auto jtarget = json.get_ptr("target");
    checkLogic(jtarget, "AsynclogRoute: target not found");
    asynclogName = jname->stringPiece().str();
    target = factory.create(*jtarget);
  }
  return { std::move(target), std::move(asynclogName) };
}

}}}  // facebook::memcache::mcrouter
