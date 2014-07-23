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
#include <utility>
#include <vector>

#include <folly/Optional.h>

#include "mcrouter/ProxyMcRequest.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/proxy.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/ShadowRouteIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Shadowing using dynamic settings.
 *
 * Always sends the request to normalRoute.
 * In addition, asynchronously sends the same request to shadowRoutes if:
 *   1) normalIndex is within settings range
 *   2) key hash is within settings range
 * Both ranges might be updated at runtime.
 * We can shadow to multiple shadow destinations for a given normal route.
 */
template <class RouteHandleIf, class ShadowPolicy>
class ShadowRoute {
 public:
  static std::string routeName() { return "shadow"; }

  ShadowRoute(std::shared_ptr<RouteHandleIf> normalRoute,
              ShadowData<RouteHandleIf> shadowData,
              size_t normalIndex,
              ShadowPolicy shadowPolicy)
      : normal_(std::move(normalRoute)),
        shadowData_(std::move(shadowData)),
        normalIndex_(normalIndex),
        shadowPolicy_(std::move(shadowPolicy)) {
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    std::vector<std::shared_ptr<RouteHandleIf>> rh = {normal_};
    for (auto& shadowData: shadowData_) {
      rh.push_back(shadowData.first);
    }
    return rh;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    std::shared_ptr<Request> shadowReq;
    folly::Optional<typename ReplyType<Operation, Request>::type> normalReply;
    for (auto iter: shadowData_) {
      if (shouldShadow(req, iter.second)) {
        if (!shadowReq) {
          shadowReq = std::make_shared<Request>(
            shadowPolicy_.updateRequestForShadowing(req, Operation()));
        }
        if (!normalReply && shadowPolicy_.shouldDelayShadow(req, Operation())) {
          normalReply = normal_->route(*shadowReq, Operation());
        }
        auto shadow = iter.first;
        fiber::addTask(
          [shadow, shadowReq] () {
            shadow->route(*shadowReq, Operation());
          });
      }
    }

    if (normalReply) {
      return std::move(*normalReply);
    } else {
      return normal_->route(shadowReq ? *shadowReq : req, Operation());
    }
  }

 private:
  const std::shared_ptr<RouteHandleIf> normal_;
  ShadowData<RouteHandleIf> shadowData_;
  const size_t normalIndex_;
  ShadowPolicy shadowPolicy_;

  void attachRequestClass(ProxyMcRequest& req) const {
    req.setRequestClass(RequestClass::SHADOW);
  }

  template <class Request>
  void attachRequestClass(Request& req) const {
  }

  template <class Request>
  bool shouldShadow(const Request& req,
      std::shared_ptr<proxy_pool_shadowing_policy_t> shadowingSettings) const {
    auto data = shadowingSettings->getData();

    if (normalIndex_ < data->start_index ||
        normalIndex_ >= data->end_index) {
      return false;
    }

    assert(data->start_key_fraction >= 0.0 &&
           data->start_key_fraction <= 1.0 &&
           data->end_key_fraction >= 0.0 &&
           data->end_key_fraction <= 1.0 &&
           data->start_key_fraction <= data->end_key_fraction);

    return match_routing_key_hash(req.routingKeyHash(),
      data->start_key_fraction,
      data->end_key_fraction);
  }

};

}}}  // facebook::memcache::mcrouter
