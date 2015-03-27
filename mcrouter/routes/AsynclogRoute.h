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

#include "mcrouter/lib/McOperationTraits.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/Operation.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Async logs a failed request. It assumes the required data is available in
 * the reply.
 */
template <class RouteHandleIf>
class AsynclogRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;
  using StackContext = typename RouteHandleIf::StackContext;

  std::string routeName() const { return "asynclog:" + asynclogName_; }

  AsynclogRoute(std::shared_ptr<RouteHandleIf> rh,
                std::string asynclogName)
      : rh_(std::move(rh)),
        asynclogName_(std::move(asynclogName)) {
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return {rh_};
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx,
    StackContext&& sctx, typename DeleteLike<Operation>::Type = 0) const {

    sctx.asynclogName = &asynclogName_;
    return rh_->route(req, Operation(), ctx, std::move(sctx));
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx,
    StackContext&& sctx, OtherThanT(Operation, DeleteLike<>) = 0) const {

    return rh_->route(req, Operation(), ctx, std::move(sctx));
  }

 private:
  const std::shared_ptr<RouteHandleIf> rh_;
  const std::string asynclogName_;
};

}}}  // facebook::memcache::mcrouter
