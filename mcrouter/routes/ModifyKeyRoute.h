/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include <folly/Conv.h>
#include <folly/Optional.h>

#include "mcrouter/lib/McKey.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/gen/Memcache.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

/**
 * Modifies key of current request.
 *  set_routing_prefix if present, routing prefix of a key will be set to this
 *                     value
 *  ensure_key_prefix if key doesn't start with this value, it will be appended
 *                    to the key
 *  replace_key_prefix Replace this prefix if it exists, and append
 *                     ensure_key_prefix
 *
 * Example:
 *  ModifyKeyRoute
 *    set_routing_prefix = "/a/b/"
 *    ensure_key_prefix = "foo"
 * "/a/b/a" => "/a/b/fooa"
 * "foo" => "/a/b/foo"
 * "/b/c/o" => "/a/b/fooo"
 */
template <class RouteHandleIf>
class ModifyKeyRoute {
 public:
  static std::string routeName() {
    return "modify-key";
  }

  ModifyKeyRoute(
      std::shared_ptr<RouteHandleIf> target,
      folly::Optional<std::string> routingPrefix,
      std::string keyPrefix,
      bool modifyInplace,
      folly::Optional<std::string> keyReplace)
      : target_(std::move(target)),
        routingPrefix_(std::move(routingPrefix)),
        keyPrefix_(std::move(keyPrefix)),
        modifyInplace_(modifyInplace),
        keyReplace_(std::move(keyReplace)) {}

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    auto cloneReq = req;
    auto key = getModifiedKey(req.key());
    if (key) {
      cloneReq.key() = key.value();
    }
    t(*target_, cloneReq);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    const auto key = getModifiedKey(req.key());
    if (key) {
      return routeReqWithKey(req, key.value());
    }
    return target_->route(req);
  }

 private:
  const std::shared_ptr<RouteHandleIf> target_;
  const folly::Optional<std::string> routingPrefix_;
  const std::string keyPrefix_;
  const bool modifyInplace_;
  const folly::Optional<std::string> keyReplace_;

  folly::Optional<std::string> getModifiedKey(
      const carbon::Keys<folly::IOBuf>& reqKey) const {
    folly::StringPiece rp = routingPrefix_.hasValue() ? routingPrefix_.value()
                                                      : reqKey.routingPrefix();

    if (keyReplace_.hasValue() &&
        reqKey.keyWithoutRoute().startsWith(keyReplace_.value())) {
      auto keyWithoutRoute = reqKey.keyWithoutRoute();
      keyWithoutRoute.advance(keyReplace_.value().size());
      return folly::to<std::string>(rp, keyPrefix_, keyWithoutRoute);
    } else if (!reqKey.keyWithoutRoute().startsWith(keyPrefix_)) {
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

  template <class Request>
  ReplyT<Request> routeReqWithKey(const Request& req, folly::StringPiece key)
      const {
    auto err = isKeyValid(key);
    if (err != mc_req_err_valid) {
      return createReply<Request>(
          ErrorReply,
          "ModifyKeyRoute: invalid key: " +
              std::string(mc_req_err_to_string(err)));
    }
    auto cloneReq = req;
    cloneReq.key() = key;
    return target_->route(cloneReq);
  }
};
}
}
} // facebook::memcache::mcrouter
