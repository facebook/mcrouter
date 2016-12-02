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

#include "mcrouter/lib/McOperationTraits.h"
#include "mcrouter/lib/network/CarbonMessageTraits.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Async logs a failed request. It assumes the required data is available in
 * the reply.
 */
template <class RouterInfo>
class AsynclogRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;

 public:
  std::string routeName() const { return "asynclog:" + asynclogName_; }

  AsynclogRoute(std::shared_ptr<RouteHandleIf> rh, std::string asynclogName)
      : rh_(std::move(rh)), asynclogName_(std::move(asynclogName)) {}

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*rh_, req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req, DeleteLikeT<Request> = 0) const {
    return fiber_local<RouterInfo>::runWithLocals([this, &req]() {
      fiber_local<RouterInfo>::setAsynclogName(asynclogName_);
      return rh_->route(req);
    });
  }

  template <class Request>
  ReplyT<Request> route(const Request& req,
                        OtherThanT<Request, DeleteLike<>> = 0) const {

    return rh_->route(req);
  }
 private:
  const std::shared_ptr<RouteHandleIf> rh_;
  const std::string asynclogName_;
};

}}}  // facebook::memcache::mcrouter
