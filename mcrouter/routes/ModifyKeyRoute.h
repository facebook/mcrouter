/*
 *  Copyright (c) 2016, Facebook, Inc.
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

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/McKey.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Modifies key of current request.
 *  set_routing_prefix if present, routing prefix of a key will be set to this
 *                     value
 *  ensure_key_prefix if key doesn't start with this value, it will be appended
 *                    to the key
 *
 * Example:
 *  ModifyKeyRoute
 *    set_routing_prefix = "/a/b/"
 *    ensure_key_prefix = "foo"
 * "/a/b/a" => "/a/b/fooa"
 * "foo" => "/a/b/foo"
 * "/b/c/o" => "/a/b/fooo"
 */
class ModifyKeyRoute {
 public:
  static std::string routeName() { return "modify-key"; }

  ModifyKeyRoute(McrouterRouteHandlePtr target,
                 folly::Optional<std::string> routingPrefix,
                 std::string keyPrefix);

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    folly::StringPiece rp = routingPrefix_.hasValue()
      ? routingPrefix_.value()
      : req.key().routingPrefix();
    auto cloneReq = req;
    if (!req.key().keyWithoutRoute().startsWith(keyPrefix_)) {
      auto key =
          folly::to<std::string>(rp, keyPrefix_, req.key().keyWithoutRoute());
      cloneReq.key() = key;
    } else if (routingPrefix_.hasValue() && rp != req.key().routingPrefix()) {
      cloneReq.key() = folly::to<std::string>(rp, req.key().keyWithoutRoute());
    }
    t(*target_, cloneReq);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    folly::StringPiece rp = routingPrefix_.hasValue()
      ? routingPrefix_.value()
      : req.key().routingPrefix();

    if (!req.key().keyWithoutRoute().startsWith(keyPrefix_)) {
      auto key =
          folly::to<std::string>(rp, keyPrefix_, req.key().keyWithoutRoute());
      return routeReqWithKey(req, key);
    } else if (routingPrefix_.hasValue() && rp != req.key().routingPrefix()) {
      auto key = folly::to<std::string>(rp, req.key().keyWithoutRoute());
      return routeReqWithKey(req, key);
    }
    return target_->route(req);
  }

 private:
  const McrouterRouteHandlePtr target_;
  const folly::Optional<std::string> routingPrefix_;
  const std::string keyPrefix_;

  template <class Request>
  ReplyT<Request>
  routeReqWithKey(const Request& req, folly::StringPiece key) const {
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

}}}  // facebook::memcache::mcrouter
