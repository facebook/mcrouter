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
#include <folly/ScopeGuard.h>
#include <folly/experimental/fibers/FiberManager.h>

#include "mcrouter/ClientPool.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/McOperation.h"
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
  using ContextPtr = std::shared_ptr<ProxyRequestContext>;

  std::string routeName() const;

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
    const Request& req, Operation, const ContextPtr& ctx) const {

    ctx->recordDestination(*client_);
    return {};
  }

  template <int Op>
  ProxyMcReply route(
    const ProxyMcRequest& req, McOperation<Op>, const ContextPtr& ctx) const {

    if (!destination_->may_send()) {
      ProxyMcReply reply(TkoReply);
      reply.setDestination(client_);
      ctx->onRequestRefused(req, reply);
      return reply;
    }

    if (ctx->recording()) {
      ctx->recordDestination(*client_);
      return ProxyMcReply(DefaultReply, McOperation<Op>());
    }

    auto proxy = &ctx->proxy();
    if (req.getRequestClass() == RequestClass::SHADOW) {
      if (proxy->opts.target_max_shadow_requests > 0 &&
          pendingShadowReqs_ >= proxy->opts.target_max_shadow_requests) {
        ProxyMcReply reply(ErrorReply);
        reply.setDestination(client_);
        ctx->onRequestRefused(req, reply);
        return reply;
      }
      auto& mutableCounter = const_cast<size_t&>(pendingShadowReqs_);
      ++mutableCounter;
    }

    auto shadowReqsCountGuard = folly::makeGuard([this, &req]() {
      if (req.getRequestClass() == RequestClass::SHADOW) {
        auto& mutableCounter = const_cast<size_t&>(pendingShadowReqs_);
        --mutableCounter;
      }
    });

    auto& destination = destination_;

    DestinationRequestCtx dctx;
    auto newReq = McRequest::cloneFrom(req, !client_->keep_routing_prefix);

    auto reply = ProxyMcReply(
      destination->send(newReq, McOperation<Op>(), dctx,
                        client_->server_timeout));
    ctx->onReplyReceived(*client_,
                         req,
                         reply,
                         dctx.startTime,
                         dctx.endTime,
                         McOperation<Op>());

    // For AsynclogRoute
    if (reply.isFailoverError()) {
      reply.setDestination(client_);
    }

    return reply;
  }

 private:
  std::shared_ptr<const ProxyClientCommon> client_;
  std::shared_ptr<ProxyDestination> destination_;
  size_t pendingShadowReqs_{0};
};

}}}  // facebook::memcache::mcrouter
