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

namespace facebook { namespace memcache { namespace mcrouter {

ModifyKeyRoute::ModifyKeyRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {
  auto jtarget = json.get_ptr("target");
  checkLogic(jtarget, "ModifyKeyRoute: no target");
  target_ = factory.create(*jtarget);

  if (auto jroutingPrefix = json.get_ptr("set_routing_prefix")) {
    auto rp = jroutingPrefix->stringPiece();
    if (rp.empty()) {
      routingPrefix_ = "";
    } else {
      try {
        routingPrefix_ = RoutingPrefix(rp).str();
      } catch (const std::exception& e) {
        throw std::logic_error("ModifyKeyRoute: set_routing_prefix: " +
                               std::string(e.what()));
      }
    }
  }
  if (auto jkeyPrefix = json.get_ptr("ensure_key_prefix")) {
    keyPrefix_ = jkeyPrefix->stringPiece().str();
    auto err = mc_client_req_key_check(to<nstring_t>(keyPrefix_));
    checkLogic(keyPrefix_.empty() || err == mc_req_err_valid,
               "ModifyKeyRoute: invalid key prefix '{}', {}", keyPrefix_,
               mc_req_err_to_string(err));
  }
}

McrouterRouteHandlePtr
makeModifyKeyRoute(RouteHandleFactory<McrouterRouteHandleIf>& factory,
                   const folly::dynamic& json) {
  return std::make_shared<McrouterRouteHandle<ModifyKeyRoute>>(factory, json);
}

}}}
