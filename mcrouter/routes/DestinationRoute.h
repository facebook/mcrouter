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

#include <folly/experimental/fibers/FiberManager.h>
#include <folly/Memory.h>
#include <folly/Optional.h>
#include <folly/ScopeGuard.h>

#include "mcrouter/ClientPool.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/LeaseTokenMap.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Routes a request to a single ProxyClient.
 * This is the lowest level in Mcrouter's RouteHandle tree.
 */
class DestinationRoute {
 public:
  std::string routeName() const;

  /**
   * Spool failed delete request to disk.
   *
   * @return  true on sucess, false otherwise
   */
  bool spool(const McRequest& req) const;

  std::string keyWithFailoverTag(folly::StringPiece fullKey) const;

  /**
   * @param client Client to send request to
   * @param destination The destination where the request is to be sent
   */
  DestinationRoute(std::shared_ptr<const ProxyClientCommon> client,
                   std::shared_ptr<ProxyDestination> destination) :
      client_(std::move(client)),
      destination_(std::move(destination)) {
  }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    auto& ctx = fiber_local::getSharedCtx();
    if (ctx) {
      ctx->recordDestination(*client_);
    }
  }

  McReply route(const McRequest& req, McOperation<mc_op_lease_get> op) const {
    auto reply = routeImpl(req, op);
    if (reply.leaseToken() > 1 &&
        (fiber_local::getRequestClass().is(RequestClass::kFailover) ||
         LeaseTokenMap::conflicts(reply.leaseToken()))) {
      auto& ctx = fiber_local::getSharedCtx();
      if (auto leaseTokenMap = ctx->proxy().router().leaseTokenMap()) {
        auto specialToken = leaseTokenMap->insert(
            reply.leaseToken(), client_->ap, client_->server_timeout);
        reply.setLeaseToken(specialToken);
      }
    }
    return reply;
  }

  template <class Operation>
  McReply route(const McRequest& req, Operation,
                typename DeleteLike<Operation>::Type = 0) const {
    auto reply = routeImpl(req, Operation());
    if (reply.isFailoverError() && spool(req)) {
      reply = McReply(DefaultReply, Operation());
      reply.setDestination(client_->ap);
    }
    return reply;
  }

  template <class Operation>
  McReply route(const McRequest& req, Operation,
                OtherThanT(Operation, DeleteLike<>) = 0) const {
    return routeImpl(req, Operation());
  }

  template <int Op>
  McReply routeImpl(const McRequest& req, McOperation<Op>) const {
    auto& ctx = fiber_local::getSharedCtx();
    auto now = nowUs();
    McReply reply(ErrorReply);
    SCOPE_EXIT {
      reply.setDestination(client_->ap);
    };

    if (!destination_->may_send()) {
      reply = McReply(TkoReply);
      ctx->onReplyReceived(*client_,
                           req,
                           reply,
                           now,
                           now,
                           McOperation<Op>());
      return reply;
    }

    if (ctx->recording()) {
      ctx->recordDestination(*client_);
      reply = McReply(DefaultReply, McOperation<Op>());
      return reply;
    }

    auto proxy = &ctx->proxy();
    auto requestClass = fiber_local::getRequestClass();
    if (requestClass.is(RequestClass::kShadow)) {
      if (proxy->router().opts().target_max_shadow_requests > 0 &&
          pendingShadowReqs_ >=
          proxy->router().opts().target_max_shadow_requests) {
        reply = McReply(ErrorReply);
        ctx->onReplyReceived(*client_,
                             req,
                             reply,
                             now,
                             now,
                             McOperation<Op>());
        return reply;
      }
      auto& mutableCounter = const_cast<size_t&>(pendingShadowReqs_);
      ++mutableCounter;
    }

    SCOPE_EXIT {
      if (requestClass.is(RequestClass::kShadow)) {
        auto& mutableCounter = const_cast<size_t&>(pendingShadowReqs_);
        --mutableCounter;
      }
    };

    DestinationRequestCtx dctx(now);
    folly::Optional<McRequest> newReq;
    if (!client_->keep_routing_prefix && !req.routingPrefix().empty()) {
      newReq.emplace(req.clone());
      newReq->stripRoutingPrefix();
    }

    if (fiber_local::getFailoverTag()) {
      if (!newReq) {
        newReq.emplace(req.clone());
      }
      auto newKey = keyWithFailoverTag(newReq->fullKey());
      /* It's always safe to not append a failover tag */
      if (newKey.size() <= MC_KEY_MAX_LEN) {
        newReq->setKey(std::move(newKey));
      }
    }

    const McRequest& reqToSend = newReq ? *newReq : req;
    reply = destination_->send(reqToSend, McOperation<Op>(), dctx,
                               client_->server_timeout);
    ctx->onReplyReceived(*client_,
                         reqToSend,
                         reply,
                         dctx.startTime,
                         dctx.endTime,
                         McOperation<Op>());
    return reply;
  }

 private:
  std::shared_ptr<const ProxyClientCommon> client_;
  std::shared_ptr<ProxyDestination> destination_;
  size_t pendingShadowReqs_{0};
};

}}}  // facebook::memcache::mcrouter
