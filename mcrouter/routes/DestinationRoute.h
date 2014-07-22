/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <mcrouter/config.h>

#include "folly/Format.h"
#include "folly/Memory.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyRequest.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/RecordingContext.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/proxy.h"
#include "mcrouter/route.h"

namespace facebook { namespace memcache { namespace mcrouter {

struct DestinationRequestCtx {
  folly::Optional<FiberPromise<void>> promise;
  proxy_request_t* preq{nullptr};
  int64_t startTime{0};
  int64_t endTime{0};
  McMsgRef reply;
  mc_res_t result{mc_res_unknown};

  explicit DestinationRequestCtx(proxy_request_t* proxyReq)
      : preq(proxyReq),
        startTime(nowUs()) {
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
    return folly::format("host|pool={}|id={}|ssl={}|ap={}",
      client_->pool ? client_->pool->getName() : "NOPOOL",
      client_->indexInPool,
      client_->useSsl,
      client_->ap.toString()).str();
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
  static
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) {

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

    auto deleteTime = client_->pool ? client_->pool->delete_time : 0;
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

  template <class Request, int M>
  McMsgRef generateMsg(const Request& req, McOperation<M>) const {
    if (client_->keep_routing_prefix) {
      return req.dependentMsg((mc_op_t)M);
    }

    return req.dependentMsgStripRoutingPrefix((mc_op_t)M);
  }

  template <typename Operation>
  ProxyMcReply routeImpl(const ProxyMcRequest& req, Operation) const {
    auto msg = generateMsg(req, Operation());

    if (!destination_->may_send(msg)) {
      update_send_stats(req.context().proxyRequest().proxy,
                        msg,
                        PROXY_SEND_REMOTE_ERROR);
      ProxyMcReply reply(TkoReply);
      reply.setDestination(client_);
      return reply;
    }

    auto& destination = destination_;

    DestinationRequestCtx ctx(&req.context().proxyRequest());

    fiber::await(
      [&destination, &msg, &req, &ctx](FiberPromise<void> promise) {
        ctx.promise = std::move(promise);
        destination->send(std::move(msg),
                          &ctx,
                          req.context().senderId());
      });
    auto reply = ProxyMcReply(ctx.result, std::move(ctx.reply));

    req.context().onReplyReceived(*client_,
                                  req,
                                  reply,
                                  ctx.startTime,
                                  ctx.endTime,
                                  Operation());

    if (reply.isError()) {
      reply.setDestination(client_);
    }

    /* Convert connect errors to TKO for higher levels,
       since they don't know any better.  TKO result triggers fast failover
       among other things. */
    if (reply.isConnectError()) {
      reply.setResult(mc_res_tko);
    }

    return reply;
  }

  template <typename Operation>
  McReply routeImpl(const RecordingMcRequest& req, Operation) const {
    auto msg = generateMsg(req, Operation());
    if (!destination_->may_send(msg)) {
      return McReply(TkoReply);
    }

    req.context().recordDestination(*client_);
    return NullRoute<RouteHandleIf>::route(req, Operation());
  }
};

}}}  // facebook::memcache::mcrouter
