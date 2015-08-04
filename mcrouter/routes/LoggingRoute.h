/*
 *  Copyright (c) 2015, Facebook, Inc.
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

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Forwards requests to the child route, then logs the request and response.
 */
class LoggingRoute {
 public:
  static std::string routeName() {
    return "logging";
  }

  explicit LoggingRoute(McrouterRouteHandlePtr rh)
    : child_(std::move(rh)) {}

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    if (child_) {
      t(*child_, req, Operation());
    }
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) {

    typename ReplyType<Operation,Request>::type reply;
    if (child_ == nullptr) {
      reply = NullRoute<McrouterRouteHandleIf>::route(req, Operation());
    } else {
      reply = child_->route(req, Operation());
    }

    // Pull the IP (if available) out of the saved request
    auto& ctx = mcrouter::fiber_local::getSharedCtx();
    auto& ip = ctx->userIpAddress();
    folly::StringPiece userIp;
    if (!ip.empty()) {
      userIp = ip;
    } else {
      userIp = "N/A";
    }

    auto& callback = ctx->proxy().router().postprocessCallback();
    if (callback) {
      if (reply.isHit()) {
        callback(req.fullKey(), reply.flags(), reply.valueRangeSlow(),
                 Operation::name, userIp);
      }
    } else {
      LOG(INFO) << "request key: " << req.fullKey()
                << " response: " << mc_res_to_string(reply.result())
                << " responseLength: " << reply.value().computeChainDataLength()
                << " user ip: " << userIp;
    }
    return reply;
  }

 private:
  const McrouterRouteHandlePtr child_;
};

}}}
