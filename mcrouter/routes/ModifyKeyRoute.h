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

#include "mcrouter/lib/McKey.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/gen/Memcache.h"
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

  ModifyKeyRoute(
      McrouterRouteHandlePtr target,
      folly::Optional<std::string> routingPrefix,
      std::string keyPrefix,
      bool modifyInplace);

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
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
  const McrouterRouteHandlePtr target_;
  const folly::Optional<std::string> routingPrefix_;
  const std::string keyPrefix_;
  const bool modifyInplace_;

  folly::Optional<std::string> getModifiedKey(
      const carbon::Keys<folly::IOBuf>& reqKey) const;

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
