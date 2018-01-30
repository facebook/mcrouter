/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <folly/Conv.h>

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyRequestContextTyped.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/network/AccessPoint.h"
#include "mcrouter/lib/network/ReplyStatsContext.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

/**
 * Returns the error reply for each request right away
 */
template <class RouterInfo>
class ErrorRoute {
 public:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;

  std::string routeName() const {
    const std::string name = "error";
    if (valueToSet_.empty()) {
      return name;
    }
    return folly::to<std::string>(name, "|", valueToSet_);
  }

  template <class Request>
  void traverse(const Request&, const RouteHandleTraverser<RouteHandleIf>&)
      const {}

  explicit ErrorRoute(std::string valueToSet = "")
      : valueToSet_(std::move(valueToSet)) {}

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    auto reply = createReply<Request>(ErrorReply, valueToSet_);
    if (auto& ctx = fiber_local<RouterInfo>::getSharedCtx()) {
      auto now = nowUs();
      AccessPoint ap;
      ReplyStatsContext replyContext;
      ctx->onReplyReceived(
          routeName() /* poolName */,
          ap,
          folly::StringPiece(),
          req,
          reply,
          fiber_local<RouterInfo>::getRequestClass(),
          now,
          now,
          replyContext);
    }
    return reply;
  }

 private:
  const std::string valueToSet_;
};

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr createErrorRoute(std::string valueToSet) {
  return makeRouteHandleWithInfo<RouterInfo, ErrorRoute>(std::move(valueToSet));
}

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeErrorRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>&,
    const folly::dynamic& json) {
  checkLogic(
      json.isObject() || json.isString() || json.isNull(),
      "ErrorRoute: should be string or object");
  std::string response;
  if (json.isString()) {
    response = json.getString();
  } else if (json.isObject()) {
    if (auto jResponse = json.get_ptr("response")) {
      checkLogic(jResponse->isString(), "ErrorRoute: response is not a string");
      response = jResponse->getString();
    }
  }
  return createErrorRoute<RouterInfo>(std::move(response));
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
