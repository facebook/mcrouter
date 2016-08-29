/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ModifyKeyRoute.h"

#include <folly/dynamic.h>

#include "mcrouter/RoutingPrefix.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache { namespace mcrouter {

ModifyKeyRoute::ModifyKeyRoute(
    McrouterRouteHandlePtr target,
    folly::Optional<std::string> routingPrefix,
    std::string keyPrefix,
    bool modifyInplace)
    : target_(std::move(target)),
      routingPrefix_(std::move(routingPrefix)),
      keyPrefix_(std::move(keyPrefix)),
      modifyInplace_(modifyInplace) {}

folly::Optional<std::string> ModifyKeyRoute::getModifiedKey(
    const carbon::Keys<folly::IOBuf>& reqKey) const {
  folly::StringPiece rp = routingPrefix_.hasValue() ? routingPrefix_.value()
                                                    : reqKey.routingPrefix();

  if (!reqKey.keyWithoutRoute().startsWith(keyPrefix_)) {
    auto keyWithoutRoute = reqKey.keyWithoutRoute();
    if (modifyInplace_ && keyWithoutRoute.size() >= keyPrefix_.size()) {
      keyWithoutRoute.advance(keyPrefix_.size());
    }
    return folly::to<std::string>(rp, keyPrefix_, keyWithoutRoute);
  } else if (routingPrefix_.hasValue() && rp != reqKey.routingPrefix()) {
    return folly::to<std::string>(rp, reqKey.keyWithoutRoute());
  }
  return folly::none;
}

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
    keyPrefix = jkeyPrefix->getString();
    auto err = isKeyValid(keyPrefix);
    checkLogic(keyPrefix.empty() || err == mc_req_err_valid,
               "ModifyKeyRoute: invalid key prefix '{}', {}", keyPrefix,
               mc_req_err_to_string(err));
  }

  bool modifyInplace = false;
  if (auto joverwrite = json.get_ptr("modify_inplace")) {
    checkLogic(
        joverwrite->isBool(), "ModifyKeyRoute: modify_inplace is not a bool");
    modifyInplace = joverwrite->asBool();
  }
  return std::make_shared<McrouterRouteHandle<ModifyKeyRoute>>(
      factory.create(*jtarget),
      std::move(routingPrefix),
      std::move(keyPrefix),
      modifyInplace);
}

}}}  // facebook::memcache::mcrouter
