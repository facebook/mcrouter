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

#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/ScopeGuard.h>

#include "mcrouter/ClientPool.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyMcReply.h"
#include "mcrouter/ProxyMcRequest.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/route.h"

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
template <class RouteHandleIf>
class DestinationRoute {
 public:
  std::string routeName() const {
    return folly::sformat("host|pool={}|id={}|ssl={}|ap={}|timeout={}ms",
      client_->pool.getName(),
      client_->indexInPool,
      client_->useSsl,
      client_->ap.toString(),
      client_->server_timeout.count());
  }

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
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    req.context().recordDestination(*client_);
    return {};
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, OtherThanT(Operation, DeleteLike<>) = 0)
    const {

    return routeImpl(req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    typename DeleteLike<Operation>::Type = 0) const {

    auto deleteTime = client_->deleteTime;
    if (deleteTime != 0 && (req.exptime() == 0 || req.exptime() > deleteTime)) {
      auto mutReq = req.clone();
      mutReq.setExptime(deleteTime);
      return routeImpl(std::move(mutReq), Operation());
    }
    return routeImpl(req, Operation());
  }

 private:
  std::shared_ptr<const ProxyClientCommon> client_;
  std::shared_ptr<ProxyDestination> destination_;
  size_t pendingShadowReqs_{0};

  template <int Op>
  ProxyMcReply routeImpl(const ProxyMcRequest& req, McOperation<Op>) const {

    if (!destination_->may_send()) {
      ProxyMcReply reply(TkoReply);
      reply.setDestination(client_);
      req.context().onRequestRefused(req, reply);
      return reply;
    }

    if (req.context().recording()) {
      req.context().recordDestination(*client_);
      return NullRoute<RouteHandleIf>::route(req, McOperation<Op>());
    }

    auto proxy = &req.context().proxy();
    if (req.getRequestClass() == RequestClass::SHADOW) {
      if (proxy->opts.target_max_shadow_requests > 0 &&
          pendingShadowReqs_ >= proxy->opts.target_max_shadow_requests) {
        ProxyMcReply reply(ErrorReply);
        reply.setDestination(client_);
        req.context().onRequestRefused(req, reply);
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

    DestinationRequestCtx ctx;
    auto newReq = McRequest::cloneFrom(req, !client_->keep_routing_prefix);

    auto reply = ProxyMcReply(
      destination->send(newReq, McOperation<Op>(), ctx,
                        req.context().senderId(),
                        client_->server_timeout));
    req.context().onReplyReceived(*client_,
                                  req,
                                  reply,
                                  ctx.startTime,
                                  ctx.endTime,
                                  McOperation<Op>());

    // For AsynclogRoute
    if (reply.isFailoverError()) {
      reply.setDestination(client_);
    }

    return reply;
  }
};

}}}  // facebook::memcache::mcrouter
