/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <folly/dynamic.h>

#include "mcrouter/config-impl.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/routes/FailoverRoute.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyMcReply.h"
#include "mcrouter/ProxyMcRequest.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/FailoverWithExptimeRouteIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

const char kFailoverHostPortSeparator = '@';
const std::string kFailoverTagStart = ":failover=";

template <class RouteHandleIf>
class FailoverWithExptimeRoute {
 public:
  static std::string routeName() { return "failover-exptime"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    std::vector<std::shared_ptr<RouteHandleIf>> rh = {normal_};
    auto frh = failover_.couldRouteTo(req, Operation());
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
    const Request& req, Operation) const {

    if (!normal_) {
      return NullRoute<RouteHandleIf>::route(req, Operation());
    }

    auto reply = normal_->route(req, Operation());

    if (!reply.isError() ||
        !(GetLike<Operation>::value || UpdateLike<Operation>::value ||
          DeleteLike<Operation>::value) ||
        isFailoverDisabledForRequest(req)) {
      return reply;
    }

    bool is_failable_error =
        reply.isTko() || reply.isConnectTimeout() || reply.isDataTimeout() ||
        reply.isTryAgain() || reply.isBusy();
    if (!is_failable_error) {
      return reply;
    }

    if (((reply.isTko() || reply.isTryAgain() || reply.isBusy()) &&
         !settings_.tko.shouldFailover(Operation())) ||
        (reply.isConnectTimeout() &&
         !settings_.connectTimeout.shouldFailover(Operation())) ||
        (reply.isDataTimeout() &&
         !settings_.dataTimeout.shouldFailover(Operation()))) {
      return reply;
    }

    auto mutReq = req.clone();
    if (settings_.failoverTagging) {
      mutReq.setKey(keyWithFailoverTag(mutReq, reply));
    }
    /* 0 means infinite exptime.
       We want to set the smallest of request exptime, failover exptime. */
    if (failoverExptime_ != 0 &&
        (req.exptime() == 0 || req.exptime() > failoverExptime_)) {
      mutReq.setExptime(failoverExptime_);
    }
    return routeImpl(mutReq, Operation());
  }

 private:
  std::shared_ptr<RouteHandleIf> normal_;
  FailoverRoute<RouteHandleIf> failover_;
  uint32_t failoverExptime_;
  FailoverWithExptimeSettings settings_;

  template <class Operation>
  ProxyMcReply routeImpl(
    ProxyMcRequest& req, Operation) const {
    req.setRequestClass(RequestClass::FAILOVER);
    return failover_.route(req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const Request& req, Operation) const {
    return failover_.route(req, Operation());
  }

  bool isFailoverDisabledForRequest(const ProxyMcRequest& req) const {
    return req.context().ctx().proxyRequest().failover_disabled;
  }

  template <class Request>
  bool isFailoverDisabledForRequest(const Request& req) const {
    return false;
  }

  static std::string keyWithFailoverTag(const ProxyMcRequest& req,
                                        const ProxyMcReply& reply) {
    if (req.keyWithoutRoute() == req.routingKey()) {
      // The key doesn't have a hash stop.
      return req.fullKey().str();
    }
    auto proxyClient = reply.getDestination();
    if (proxyClient == nullptr) {
      return req.fullKey().str();
    }
    const size_t tagLength =
      kFailoverTagStart.size() +
      proxyClient->ap.getHost().size() +
      proxyClient->ap.getPort().size() +
      1; // kFailoverHostPortSeparator
    std::string failoverTag;
    failoverTag.reserve(tagLength);
    failoverTag = kFailoverTagStart;
    failoverTag += proxyClient->ap.getHost();
    if (!proxyClient->ap.getPort().empty()) {
      failoverTag += kFailoverHostPortSeparator;
      failoverTag += proxyClient->ap.getPort();
    }

    // Safety check: scrub the host and port for ':' to avoid appending
    // more than one field to the key.
    // Note: we start after the ':failover=' part of the string,
    // since we need the initial ':' and we know the remainder is safe.
    for (size_t i = kFailoverTagStart.size(); i < failoverTag.size(); i++) {
      if (failoverTag[i] == ':') {
        failoverTag[i] = '$';
      }
    }

    return req.fullKey().str() + failoverTag;
  }

  template <class Request, class Reply>
  static std::string keyWithFailoverTag(const Request& req,
                                        const Reply& reply) {
    return req.fullKey().str();
  }
};

}}}
