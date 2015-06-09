/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ModifyKeyRoute.h"

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/RoutingPrefix.h"

namespace facebook { namespace memcache { namespace mcrouter {

ModifyKeyRoute::ModifyKeyRoute(McrouterRouteHandlePtr target,
                               folly::Optional<std::string> routingPrefix,
                               std::string keyPrefix)
  : target_(std::move(target)),
    routingPrefix_(std::move(routingPrefix)),
    keyPrefix_(std::move(keyPrefix)) {}

McrouterRouteHandlePtr makeModifyKeyRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {
  auto jtarget = json.get_ptr("target");
  checkLogic(jtarget, "ModifyKeyRoute: no target");

  folly::Optional<std::string> routingPrefix;
  if (auto jroutingPrefix = json.get_ptr("set_routing_prefix")) {
    auto rp = jroutingPrefix->stringPiece();
    if (rp.empty()) {
      routingPrefix = "";
    } else {
      try {
        routingPrefix = RoutingPrefix(rp).str();
      } catch (const std::exception& e) {
        throw std::logic_error("ModifyKeyRoute: set_routing_prefix: " +
                               std::string(e.what()));
      }
    }
  }
  std::string keyPrefix;
  if (auto jkeyPrefix = json.get_ptr("ensure_key_prefix")) {
    keyPrefix = jkeyPrefix->stringPiece().str();
    auto err = mc_client_req_key_check(to<nstring_t>(keyPrefix));
    checkLogic(keyPrefix.empty() || err == mc_req_err_valid,
               "ModifyKeyRoute: invalid key prefix '{}', {}", keyPrefix,
               mc_req_err_to_string(err));
  }
  return std::make_shared<McrouterRouteHandle<ModifyKeyRoute>>(
    factory.create(*jtarget),
    std::move(routingPrefix),
    std::move(keyPrefix));
}

}}}  // facebook::memcache::mcrouter
