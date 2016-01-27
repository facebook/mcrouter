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
#include <vector>

#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/RateLimiter.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Requests sent through this route will be rate limited according
 * to settings in the RateLimiter passed to the constructor.
 *
 * See comments in TokenBucket.h for algorithm details.
 */
class RateLimitRoute {
 public:
  static std::string routeName() { return "rate-limit"; }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    t(*target_, req);
  }

  RateLimitRoute(McrouterRouteHandlePtr target, RateLimiter rl)
      : target_(std::move(target)),
        rl_(std::move(rl)) {
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    if (LIKELY(rl_.canPassThrough<Request>())) {
      return target_->route(req);
    }
    return ReplyT<Request>(DefaultReply, req);
  }

 private:
  McrouterRouteHandlePtr target_;
  RateLimiter rl_;
};

}}}  // facebook::memcache::mcrouter
