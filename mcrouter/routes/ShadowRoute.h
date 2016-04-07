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

#include <memory>
#include <utility>
#include <vector>

#include <folly/experimental/fibers/FiberManager.h>
#include <folly/Optional.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/proxy.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/ShadowRouteIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Shadowing using dynamic settings.
 *
 * Always sends the request to normalRoute.
 * In addition, asynchronously sends the same request to shadowRoutes if key
 * hash is within settings range
 * Key range might be updated at runtime.
 * We can shadow to multiple shadow destinations for a given normal route.
 */
template <class ShadowPolicy>
class ShadowRoute {
 public:
  static std::string routeName() { return "shadow"; }

  ShadowRoute(McrouterRouteHandlePtr normalRoute,
              McrouterShadowData shadowData,
              ShadowPolicy shadowPolicy)
      : normal_(std::move(normalRoute)),
        shadowData_(std::move(shadowData)),
        shadowPolicy_(std::move(shadowPolicy)) {
  }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
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
        if (iter.second->validateRepliesFlag()) {
          normalReply = normal_->route(*adjustedReq);
          // this will spawn the fiber after copying required data
          // to validate from the normal Reply
          sendAndValidateRequest(
              *normalReply , std::move(shadow), adjustedReq);

        } else {
          dispatchShadowRequest(std::move(shadow), adjustedReq);
        }
      }
    }

    if (normalReply) {
      return std::move(*normalReply);
    } else {
      return normal_->route(adjustedReq ? *adjustedReq : req);
    }
  }

 private:
  const McrouterRouteHandlePtr normal_;
  const McrouterShadowData shadowData_;
  ShadowPolicy shadowPolicy_;

  template <class Request>
  bool shouldShadow(const Request& req, ShadowSettings* settings) const {
    auto range = settings->keyRange();
    return range.first <= req.routingKeyHash() &&
                          req.routingKeyHash() <= range.second;
  }

  template <class Request>
  void dispatchShadowRequest(std::shared_ptr<McrouterRouteHandleIf> shadow,
                             std::shared_ptr<Request> adjustedReq) const;

  template <class Request>
  void sendAndValidateRequest(const ReplyT<Request>& normalReply,
                              std::shared_ptr<McrouterRouteHandleIf> shadow,
                              std::shared_ptr<Request> adjustedReq) const;

  template <class GetRequest>
  void sendAndValidateRequestGetImpl(
      const ReplyT<GetRequest>& normalReply,
      std::shared_ptr<McrouterRouteHandleIf> shadow,
      std::shared_ptr<GetRequest> adjustedReq)
      const;

  void sendAndValidateRequest(
      const McReply& normalReply,
      std::shared_ptr<McrouterRouteHandleIf> shadow,
      std::shared_ptr<McRequestWithMcOp<mc_op_get>> adjustedReq)
      const {

    sendAndValidateRequestGetImpl(normalReply, shadow, adjustedReq);
  }

  void sendAndValidateRequest(
      const TypedThriftReply<cpp2::McGetReply>& normalReply,
      std::shared_ptr<McrouterRouteHandleIf> shadow,
      std::shared_ptr<TypedThriftRequest<cpp2::McGetRequest>> adjustedReq)
      const {

    sendAndValidateRequestGetImpl(normalReply, shadow, adjustedReq);
  }
};

}}}  // facebook::memcache::mcrouter

#include "ShadowRoute-inl.h"
