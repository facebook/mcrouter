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
#include <folly/Hash.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeNullRoute();

McrouterRouteHandlePtr makeHostIdRoute(std::vector<McrouterRouteHandlePtr> rh,
    folly::StringPiece salt) {
  if (rh.empty()) {
    return makeNullRoute();
  }
  size_t hostIdHash = globals::hostid();
  if (!salt.empty()) {
    hostIdHash = folly::Hash()(hostIdHash, salt);
  }
  return std::move(rh[hostIdHash % rh.size()]);
}

McrouterRouteHandlePtr makeHostIdRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  std::vector<McrouterRouteHandlePtr> children;
  folly::StringPiece salt;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
    if (auto jsalt = json.get_ptr("salt")) {
      checkLogic(jsalt->isString(), "HostIdRoute: salt is not a string");
      salt = jsalt->stringPiece();
    }
  } else {
    children = factory.createList(json);
  }

  return makeHostIdRoute(std::move(children), salt);
}

}}}  // facebook::memcache::mcrouter
