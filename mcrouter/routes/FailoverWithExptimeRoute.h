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
#include <folly/Range.h>

#include "mcrouter/config-impl.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/routes/FailoverRoute.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/FailoverWithExptimeRouteIf.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

class FailoverWithExptimeRoute {
 public:
  static std::string routeName() { return "failover-exptime"; }

  static std::string keyWithFailoverTag(
    const folly::StringPiece fullKey,
    const AccessPoint& ap);

  template <class Operation, class Request>
  std::vector<McrouterRouteHandlePtr> couldRouteTo(
    const Request& req, Operation) const {

    std::vector<McrouterRouteHandlePtr> rh = {normal_};
    auto frh = failover_.couldRouteTo(req, Operation());
    rh.insert(rh.end(), frh.begin(), frh.end());
    return rh;
  }

  FailoverWithExptimeRoute(
    McrouterRouteHandlePtr normalTarget,
    std::vector<McrouterRouteHandlePtr> failoverTargets,
    uint32_t failoverExptime,
    FailoverWithExptimeSettings settings)
      : normal_(std::move(normalTarget)),
        failover_(std::move(failoverTargets)),
        failoverExptime_(failoverExptime),
        settings_(settings) {
  }

  FailoverWithExptimeRoute(RouteHandleFactory<McrouterRouteHandleIf>& factory,
                           const folly::dynamic& json);

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    using Reply = typename ReplyType<Operation, Request>::type;
    if (!normal_) {
      return Reply(DefaultReply, Operation());
    }

    auto reply = normal_->route(req, Operation());
    auto& ctx = fiber_local::getSharedCtx();

    if (!reply.isFailoverError() ||
        !(GetLike<Operation>::value || UpdateLike<Operation>::value ||
          DeleteLike<Operation>::value) ||
        ctx->failoverDisabled()) {
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

    auto mutReq = req.clone();
    if (settings_.failoverTagging &&
        mutReq.hasHashStop() &&
        reply.getDestination() != nullptr) {
      mutReq.setKey(keyWithFailoverTag(mutReq.fullKey(),
                                       reply.getDestination()->ap));
    }
    /* 0 means infinite exptime.
       We want to set the smallest of request exptime, failover exptime. */
    if (failoverExptime_ != 0 &&
        (req.exptime() == 0 || req.exptime() > failoverExptime_)) {
      mutReq.setExptime(failoverExptime_);
    }

    mutReq.setRequestClass(RequestClass::FAILOVER);
    return failover_.route(mutReq, Operation());
  }

 private:
  McrouterRouteHandlePtr normal_;
  FailoverRoute<McrouterRouteHandleIf> failover_;
  uint32_t failoverExptime_;
  FailoverWithExptimeSettings settings_;
};

}}}
