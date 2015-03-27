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
#include <vector>

#include <folly/dynamic.h>

#include "mcrouter/config-impl.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/routes/FailoverRoute.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/McrouterStackContext.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/FailoverWithExptimeRouteIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <class RouteHandleIf>
class FailoverWithExptimeRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;
  using StackContext = typename RouteHandleIf::StackContext;

  static std::string routeName() { return "failover-exptime"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    std::vector<std::shared_ptr<RouteHandleIf>> rh = {normal_};
    auto frh = failover_.couldRouteTo(req, Operation(), ctx);
    rh.insert(rh.end(), frh.begin(), frh.end());
    return rh;
  }

  FailoverWithExptimeRoute(
    std::shared_ptr<RouteHandleIf> normalTarget,
    std::vector<std::shared_ptr<RouteHandleIf>> failoverTargets,
    uint32_t failoverExptime,
    FailoverWithExptimeSettings settings)
      : normal_(std::move(normalTarget)),
        failover_(std::move(failoverTargets)),
        failoverExptime_(failoverExptime),
        settings_(settings) {
  }

  FailoverWithExptimeRoute(RouteHandleFactory<RouteHandleIf>& factory,
                           const folly::dynamic& json)
      : failoverExptime_(60) {

    checkLogic(json.isObject(), "FailoverWithExptimeRoute is not object");

    std::vector<std::shared_ptr<RouteHandleIf>> failoverTargets;

    if (json.count("failover")) {
      failoverTargets = factory.createList(json["failover"]);
    }

    failover_ = FailoverRoute<RouteHandleIf>(std::move(failoverTargets));

    if (json.count("normal")) {
      normal_ = factory.create(json["normal"]);
    }

    if (json.count("failover_exptime")) {
      checkLogic(json["failover_exptime"].isInt(),
                 "failover_exptime is not integer");
      failoverExptime_ = json["failover_exptime"].asInt();
    }

    if (json.count("settings")) {
      settings_ = FailoverWithExptimeSettings(json["settings"]);
    }
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx,
    StackContext&& sctx) const {

    if (!normal_) {
      return NullRoute<RouteHandleIf>::route(req, Operation(), ctx,
                                             std::move(sctx));
    }

    auto reply = normal_->route(req, Operation(), ctx, StackContext(sctx));

    if (!reply.isFailoverError() ||
        !(GetLike<Operation>::value || UpdateLike<Operation>::value ||
          DeleteLike<Operation>::value) ||
        isFailoverDisabledForRequest(ctx)) {
      return reply;
    }

    if (((reply.isTko() || reply.isConnectError() || reply.isRedirect()) &&
         !settings_.tko.shouldFailover(Operation())) ||
        (reply.isConnectTimeout() &&
         !settings_.connectTimeout.shouldFailover(Operation())) ||
        (reply.isDataTimeout() &&
         !settings_.dataTimeout.shouldFailover(Operation()))) {
      return reply;
    }

    if (settings_.failoverTagging) {
      setFailoverTag(sctx);
    }
    /* 0 means infinite exptime.
       We want to set the smallest of request exptime, failover exptime. */
    auto mutReq = req.clone();
    if (failoverExptime_ != 0 &&
        (req.exptime() == 0 || req.exptime() > failoverExptime_)) {
      mutReq.setExptime(failoverExptime_);
    }
    setRequestClass(sctx);
    return failover_.route(mutReq, Operation(), ctx, std::move(sctx));
  }

 private:
  std::shared_ptr<RouteHandleIf> normal_;
  FailoverRoute<RouteHandleIf> failover_;
  uint32_t failoverExptime_;
  FailoverWithExptimeSettings settings_;

  static bool isFailoverDisabledForRequest(
      const std::shared_ptr<ProxyRequestContext>& ctx) {
    return ctx->failoverDisabled();
  }

  template <class C>
  static bool isFailoverDisabledForRequest(const C& ctx) {
    return false;
  }

  static void setFailoverTag(McrouterStackContext& sctx) {
    sctx.failoverTag = true;
  }

  template <class C>
  static void setFailoverTag(C& sctx) { }

  static void setRequestClass(McrouterStackContext& sctx) {
    sctx.requestClass = RequestClass::FAILOVER;
  }

  template <class C>
  static void setRequestClass(C& sctx) { }
};

}}}
