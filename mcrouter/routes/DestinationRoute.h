/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

#include <folly/Format.h>
#include <folly/Optional.h>
#include <folly/ScopeGuard.h>
#include <folly/fibers/FiberManager.h>

#include "mcrouter/AsyncLog.h"
#include "mcrouter/AsyncWriter.h"
#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/carbon/FailoverUtil.h"
#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/network/gen/MemcacheMessages.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace folly {
struct dynamic;
}

namespace facebook {
namespace memcache {

template <class RouteHandleIf>
class RouteHandleFactory;

namespace mcrouter {

/**
 * Routes a request to a single ProxyDestination.
 * This is the lowest level in Mcrouter's RouteHandle tree.
 */
template <class RouterInfo, class Transport>
class DestinationRoute {
 public:
  std::string routeName() const {
    return folly::sformat(
        "host|pool={}|id={}|ap={}|timeout={}ms",
        poolName_,
        indexInPool_,
        destination_->accessPoint()->toString(),
        timeout_.count());
  }

  /**
   * @param destination The destination where the request is to be sent
   */
  DestinationRoute(
      std::shared_ptr<ProxyDestination<Transport>> destination,
      folly::StringPiece poolName,
      size_t indexInPool,
      int32_t poolStatIdx,
      std::chrono::milliseconds timeout,
      bool keepRoutingPrefix)
      : destination_(std::move(destination)),
        poolName_(poolName),
        indexInPool_(indexInPool),
        poolStatIndex_(poolStatIdx),
        timeout_(timeout),
        keepRoutingPrefix_(keepRoutingPrefix) {
    destination_->setPoolStatsIndex(poolStatIdx);
  }

  template <class Request>
  bool traverse(
      const Request& req,
      const RouteHandleTraverser<typename RouterInfo::RouteHandleIf>& t) const {
    PoolContext poolContext{
        poolName_,
        indexInPool_,
        fiber_local<RouterInfo>::getRequestClass().is(RequestClass::kShadow)};
    const auto& accessPoint = *destination_->accessPoint();
    if (auto* ctx = fiber_local<RouterInfo>::getTraverseCtx()) {
      ctx->recordDestination(poolContext, accessPoint);
    }
    return t(accessPoint, poolContext, req) &&
        !destination_->tracker()->isTko();
  }

  memcache::McDeleteReply route(const memcache::McDeleteRequest& req) const {
    auto reply = routeWithDestination(req);
    if (isFailoverErrorResult(reply.result()) && spool(req)) {
      reply = createReply(DefaultReply, req);
      reply.setDestination(destination_->accessPoint());
    }
    return reply;
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    return routeWithDestination(req);
  }

 private:
  const std::shared_ptr<ProxyDestination<Transport>> destination_;
  const folly::StringPiece poolName_;
  const size_t indexInPool_;
  const int32_t poolStatIndex_{-1};
  const std::chrono::milliseconds timeout_;
  size_t pendingShadowReqs_{0};
  const bool keepRoutingPrefix_;

  template <class Request>
  ReplyT<Request> routeWithDestination(const Request& req) const {
    auto reply = checkAndRoute(req);
    reply.setDestination(destination_->accessPoint());
    return reply;
  }

  template <class Request>
  ReplyT<Request> checkAndRoute(const Request& req) const {
    auto& ctx = fiber_local<RouterInfo>::getSharedCtx();
    carbon::Result tkoReason;
    if (!destination_->maySend(tkoReason)) {
      return constructAndLog(
          req,
          *ctx,
          TkoReply,
          folly::to<std::string>(
              "Server unavailable. Reason: ",
              carbon::resultToString(tkoReason)));
    }

    if (poolStatIndex_ >= 0) {
      ctx->setPoolStatsIndex(poolStatIndex_);
    }
    auto requestClass = fiber_local<RouterInfo>::getRequestClass();
    if (ctx->recording()) {
      bool isShadow = requestClass.is(RequestClass::kShadow);
      ctx->recordDestination(
          PoolContext{poolName_, indexInPool_, isShadow},
          *destination_->accessPoint());
      return constructAndLog(req, *ctx, DefaultReply, req);
    }

    auto proxy = &ctx->proxy();
    if (requestClass.is(RequestClass::kShadow)) {
      if (proxy->router().opts().target_max_shadow_requests > 0 &&
          pendingShadowReqs_ >=
              proxy->router().opts().target_max_shadow_requests) {
        return constructAndLog(req, *ctx, ErrorReply);
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

    return doRoute(req, *ctx);
  }

  template <class Request, class... Args>
  ReplyT<Request> constructAndLog(
      const Request& req,
      ProxyRequestContextWithInfo<RouterInfo>& ctx,
      Args&&... args) const {
    auto now = nowUs();
    auto reply = createReply<Request>(std::forward<Args>(args)...);
    RpcStatsContext rpcContext;
    ctx.onBeforeRequestSent(
        poolName_,
        *destination_->accessPoint(),
        folly::StringPiece(),
        req,
        fiber_local<RouterInfo>::getRequestClass(),
        now);
    ctx.onReplyReceived(
        poolName_,
        *destination_->accessPoint(),
        folly::StringPiece(),
        req,
        reply,
        fiber_local<RouterInfo>::getRequestClass(),
        now,
        now,
        poolStatIndex_,
        rpcContext);
    return reply;
  }

  template <class Request>
  ReplyT<Request> doRoute(
      const Request& req,
      ProxyRequestContextWithInfo<RouterInfo>& ctx) const {
    DestinationRequestCtx dctx(nowUs());
    folly::Optional<Request> newReq;
    folly::StringPiece strippedRoutingPrefix;
    if (!keepRoutingPrefix_ && !req.key().routingPrefix().empty()) {
      newReq.emplace(req);
      newReq->key().stripRoutingPrefix();
      strippedRoutingPrefix = req.key().routingPrefix();
    }

    if (fiber_local<RouterInfo>::getFailoverTag()) {
      if (!newReq) {
        newReq.emplace(req);
      }
      carbon::detail::setRequestFailover(*newReq);
    }

    const auto& reqToSend = newReq ? *newReq : req;
    ctx.onBeforeRequestSent(
        poolName_,
        *destination_->accessPoint(),
        strippedRoutingPrefix,
        reqToSend,
        fiber_local<RouterInfo>::getRequestClass(),
        dctx.startTime);
    RpcStatsContext rpcContext;
    auto reply = destination_->send(reqToSend, dctx, timeout_, rpcContext);
    ctx.onReplyReceived(
        poolName_,
        *destination_->accessPoint(),
        strippedRoutingPrefix,
        reqToSend,
        reply,
        fiber_local<RouterInfo>::getRequestClass(),
        dctx.startTime,
        dctx.endTime,
        poolStatIndex_,
        rpcContext);

    fiber_local<RouterInfo>::setServerLoad(rpcContext.serverLoad);
    return reply;
  }

  template <class Request>
  bool spool(const Request& req) const {
    auto asynclogName = fiber_local<RouterInfo>::getAsynclogName();
    if (asynclogName.empty()) {
      return false;
    }

    folly::StringPiece key =
        keepRoutingPrefix_ ? req.key().fullKey() : req.key().keyWithoutRoute();

    auto proxy = &fiber_local<RouterInfo>::getSharedCtx()->proxy();
    auto& ap = *destination_->accessPoint();
    folly::fibers::Baton b;
    auto res = false;
    auto attr = req.attributes();
    const auto asyncWriteStartUs = nowUs();
    if (auto asyncWriter = proxy->router().asyncWriter()) {
      res = asyncWriter->run([&b, &ap, &attr, proxy, key, asynclogName]() {
        if (proxy->asyncLog().writeDelete(ap, key, asynclogName, attr)) {
          proxy->stats().increment(asynclog_spool_success_stat);
        }
        b.post();
      });
    }
    if (!res) {
      MC_LOG_FAILURE(
          proxy->router().opts(),
          memcache::failure::Category::kOutOfResources,
          "Could not enqueue asynclog request (key {}, pool {})",
          key,
          asynclogName);
    } else {
      // Don't reply to the user until we safely logged the request to disk
      b.wait();
      const auto asyncWriteDurationUs = nowUs() - asyncWriteStartUs;
      proxy->stats().asyncLogDurationUs().insertSample(asyncWriteDurationUs);
      proxy->stats().increment(asynclog_requests_stat);
    }
    return true;
  }
};

template <class RouterInfo, class Transport>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeDestinationRoute(
    std::shared_ptr<ProxyDestination<Transport>> destination,
    folly::StringPiece poolName,
    size_t indexInPool,
    int32_t poolStatsIndex,
    std::chrono::milliseconds timeout,
    bool keepRoutingPrefix) {
  return makeRouteHandleWithInfo<RouterInfo, DestinationRoute, Transport>(
      std::move(destination),
      poolName,
      indexInPool,
      poolStatsIndex,
      timeout,
      keepRoutingPrefix);
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
