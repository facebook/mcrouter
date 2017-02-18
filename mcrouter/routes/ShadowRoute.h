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

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <folly/Optional.h>

#include "mcrouter/Proxy.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/DefaultShadowPolicy.h"
#include "mcrouter/routes/ShadowRouteIf.h"

namespace folly {
struct dynamic;
}

namespace facebook {
namespace memcache {

template <class RouteHandleIf>
class RouteHandleFactory;

namespace mcrouter {

/**
 * Shadowing using dynamic settings.
 *
 * Always sends the request to normalRoute.
 * In addition, asynchronously sends the same request to shadowRoutes if key
 * hash is within settings range
 * Key range might be updated at runtime.
 * We can shadow to multiple shadow destinations for a given normal route.
 */
template <class RouterInfo, class ShadowPolicy>
class ShadowRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;

 public:
  static std::string routeName() {
    return "shadow";
  }

  ShadowRoute(
      std::shared_ptr<RouteHandleIf> normalRoute,
      ShadowData<RouterInfo> shadowData,
      ShadowPolicy shadowPolicy)
      : normal_(std::move(normalRoute)),
        shadowData_(std::move(shadowData)),
        shadowPolicy_(std::move(shadowPolicy)) {}

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*normal_, req);
    for (auto& shadowData : shadowData_) {
      t(*shadowData.first, req);
    }
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    std::shared_ptr<Request> adjustedReq;
    folly::Optional<ReplyT<Request>> normalReply;
    for (const auto& iter : shadowData_) {
      if (shouldShadow(req, iter.second.get())) {
        if (!adjustedReq) {
          adjustedReq = std::make_shared<Request>(
              shadowPolicy_.updateRequestForShadowing(req));
        }
        if (!normalReply && shadowPolicy_.shouldDelayShadow(req)) {
          normalReply = normal_->route(*adjustedReq);
        }
        auto shadow = iter.first;
        dispatchShadowRequest(std::move(shadow), adjustedReq);
      }
    }

    if (normalReply) {
      return std::move(*normalReply);
    } else {
      return normal_->route(adjustedReq ? *adjustedReq : req);
    }
  }

 private:
  const std::shared_ptr<RouteHandleIf> normal_;
  const ShadowData<RouterInfo> shadowData_;
  ShadowPolicy shadowPolicy_;

  template <class Request>
  bool shouldShadow(const Request& req, ShadowSettings* settings) const {
    // If configured to use an explicit list of keys to be shadowed, check for
    // req.key() in that list. Otherwise, decide to shadow based on keyRange().
    const auto& keysToShadow = settings->keysToShadow();
    if (!keysToShadow.empty()) {
      const auto hashAndKeyToFind =
          std::make_tuple(req.key().routingKeyHash(), req.key().routingKey());
      return std::binary_search(
          keysToShadow.begin(),
          keysToShadow.end(),
          hashAndKeyToFind,
          std::less<std::tuple<uint32_t, folly::StringPiece>>());
    }

    auto range = settings->keyRange();
    return range.first <= req.key().routingKeyHash() &&
        req.key().routingKeyHash() <= range.second;
  }

  template <class Request>
  void dispatchShadowRequest(
      std::shared_ptr<RouteHandleIf> shadow,
      std::shared_ptr<Request> adjustedReq) const;
};

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeShadowRouteDefault(
    std::shared_ptr<typename RouterInfo::RouteHandleIf> normalRoute,
    ShadowData<RouterInfo> shadowData,
    DefaultShadowPolicy shadowPolicy);

template <class RouterInfo>
std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>>
makeShadowRoutes(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json,
    std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> children,
    ProxyBase& proxy,
    ExtraRouteHandleProviderIf<RouterInfo>& extraProvider);

template <class RouterInfo>
std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>>
makeShadowRoutes(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json,
    ProxyBase& proxy,
    ExtraRouteHandleProviderIf<RouterInfo>& extraProvider);

} // mcrouter
} // memcache
} // facebook

#include "ShadowRoute-inl.h"
