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
#include <folly/ScopeGuard.h>
#include <folly/experimental/fibers/FiberManager.h>

#include "mcrouter/ClientPool.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/route.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

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
  std::string routeName() const;

  /**
   * Spool failed delete request to disk.
   *
   * @return  true on sucess, false otherwise
   */
  bool spool(const McRequest& req) const;

  std::string keyWithFailoverTag(const folly::StringPiece fullKey) const;

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
  std::vector<McrouterRouteHandlePtr> couldRouteTo(
    const Request& req, Operation) const {
    auto& ctx = fiber_local::getSharedCtx();
    if (ctx) {
      ctx->recordDestination(*client_);
    }
    return {};
  }

  template <class Operation>
  McReply route(const McRequest& req, Operation,
                typename DeleteLike<Operation>::Type = 0) const {
    auto reply = routeImpl(req, Operation());
    if (reply.isFailoverError() && spool(req)) {
      return McReply(DefaultReply, Operation());
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

    if (!destination_->may_send()) {
      McReply reply(TkoReply);
      ctx->onRequestRefused(reply);
      return reply;
    }

    if (ctx->recording()) {
      ctx->recordDestination(*client_);
      return McReply(DefaultReply, McOperation<Op>());
    }

    auto proxy = &ctx->proxy();
    auto requestClass = fiber_local::getRequestClass();
    if (requestClass == RequestClass::SHADOW) {
      if (proxy->opts.target_max_shadow_requests > 0 &&
          pendingShadowReqs_ >= proxy->opts.target_max_shadow_requests) {
        McReply reply(ErrorReply);
        ctx->onRequestRefused(reply);
        return reply;
      }
      auto& mutableCounter = const_cast<size_t&>(pendingShadowReqs_);
      ++mutableCounter;
    }

    auto shadowReqsCountGuard = folly::makeGuard([this, &req, requestClass]() {
      if (requestClass == RequestClass::SHADOW) {
        auto& mutableCounter = const_cast<size_t&>(pendingShadowReqs_);
        --mutableCounter;
      }
    });

    auto& destination = destination_;

    DestinationRequestCtx dctx;
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
    auto reply = destination->send(reqToSend, McOperation<Op>(), dctx,
                                   client_->server_timeout);
    ctx->onReplyReceived(*client_,
                         req,
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
