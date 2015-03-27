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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <folly/Memory.h>
#include <folly/Optional.h>
#include <folly/Range.h>
#include <folly/ScopeGuard.h>

#include "mcrouter/ClientPool.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/McrouterStackContext.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyRequestContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

struct DestinationRequestCtx {
  int64_t startTime{0};
  int64_t endTime{0};

  DestinationRequestCtx() : startTime(nowUs()) {
  }
};

/**
 * Routes a request to a single ProxyClient.
 * This is the lowest level in Mcrouter's RouteHandle tree.
 */
class DestinationRoute {
 public:
  using ContextPtr = std::shared_ptr<ProxyRequestContext>;
  using StackContext = McrouterStackContext;

  std::string routeName() const;

  std::string keyWithFailoverTag(const McRequest& req) const;

  /**
   * @param client Client to send request to
   * @param destination The destination where the request is to be sent
   */
  DestinationRoute(std::shared_ptr<const ProxyClientCommon> client,
                   std::shared_ptr<ProxyDestination> destination) :
      client_(std::move(client)),
      destination_(std::move(destination)) {
  }

  bool spool(const McRequest& req, proxy_t* proxy,
             McrouterStackContext&& sctx) const;

  template <class Operation, class Request>
  std::vector<std::shared_ptr<McrouterRouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    ctx->recordDestination(*client_);
    return {};
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx, StackContext&& sctx,
    OtherThanT(Operation, DeleteLike<>) = 0) const {

    return routeImpl(req, Operation(), ctx, std::move(sctx));
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx, StackContext&& sctx,
    typename DeleteLike<Operation>::Type = 0) const {

    auto reply = routeImpl(req, Operation(), ctx, std::move(sctx));
    if (reply.isFailoverError() && spool(req, &ctx->proxy(), std::move(sctx))) {
      return McReply(DefaultReply, Operation());;
    }
    return reply;
  }

 private:
  std::shared_ptr<const ProxyClientCommon> client_;
  std::shared_ptr<ProxyDestination> destination_;
  size_t pendingShadowReqs_{0};

  template <int Op>
  McReply routeImpl(
    const McRequest& req, McOperation<Op>, const ContextPtr& ctx,
    StackContext&& sctx) const {

    if (!destination_->may_send()) {
      McReply reply(TkoReply);
      ctx->onRequestRefused(req, sctx.requestClass, reply);
      return reply;
    }

    if (ctx->recording()) {
      ctx->recordDestination(*client_);
      return McReply(DefaultReply, McOperation<Op>());
    }

    auto proxy = &ctx->proxy();
    if (sctx.requestClass == RequestClass::SHADOW) {
      if (proxy->opts.target_max_shadow_requests > 0 &&
          pendingShadowReqs_ >= proxy->opts.target_max_shadow_requests) {
        McReply reply(ErrorReply);
        ctx->onRequestRefused(req, sctx.requestClass, reply);
        return reply;
      }
      auto& mutableCounter = const_cast<size_t&>(pendingShadowReqs_);
      ++mutableCounter;
    }

    auto shadowReqsCountGuard = folly::makeGuard([this, &req, &sctx]() {
      if (sctx.requestClass == RequestClass::SHADOW) {
        auto& mutableCounter = const_cast<size_t&>(pendingShadowReqs_);
        --mutableCounter;
      }
    });

    folly::Optional<McRequest> newReq;
    if (!client_->keep_routing_prefix && !req.routingPrefix().empty()) {
      newReq.emplace(req.clone());
      newReq->stripRoutingPrefix();
    }
    if (sctx.failoverTag) {
      if (!newReq) {
        newReq.emplace(req.clone());
      }
      newReq->setKey(keyWithFailoverTag(*newReq));
    }

    DestinationRequestCtx dctx;
    const McRequest& reqToSend = newReq ? *newReq : req;
    auto reply = destination_->send(reqToSend, McOperation<Op>(), dctx,
                                    client_->server_timeout);
    ctx->onReplyReceived(*client_,
                         req,
                         sctx.requestClass,
                         reply,
                         dctx.startTime,
                         dctx.endTime,
                         McOperation<Op>());
    return reply;
  }
};

}}}  // facebook::memcache::mcrouter
