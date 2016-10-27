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

#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include <folly/Conv.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/network/CarbonMessageTraits.h"

namespace facebook { namespace memcache { namespace mcrouter {

enum class ModifyExptimeAction {
  Set,
  Min
};

const char* actionToString(ModifyExptimeAction action);

/**
 * Modifies exptime of a request.
 * Note that exptime is not modified for delete requests
 * If action == "set", applies a new expiration time.
 * If action == "min", applies a minimum of
 * (old expiration time, new expiration time).
 *
 * Note: 0 means infinite exptime.
 */
template <class RouteHandleIf>
class ModifyExptimeRoute {
 public:
  std::string routeName() const {
    return folly::sformat("modify-exptime|{}|exptime={}",
                          actionToString(action_), exptime_);
  }

  ModifyExptimeRoute(
      std::shared_ptr<RouteHandleIf> target,
      int32_t exptime,
      ModifyExptimeAction action)
      : target_(std::move(target)), exptime_(exptime), action_(action) {
    assert(action_ != ModifyExptimeAction::Min || exptime_ != 0);
  }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*target_, req);
  }

  template <class Request>
  typename std::enable_if<
      Request::hasExptime && OtherThan<Request, DeleteLike<>>::value,
      ReplyT<Request>>::type
  route(const Request& req) const {
    switch (action_) {
      case ModifyExptimeAction::Set: {
        auto mutReq = req;
        mutReq.exptime() = exptime_;
        return target_->route(mutReq);
      }
      case ModifyExptimeAction::Min: {
        /* 0 means infinite exptime. Set minimum of request exptime, exptime. */
        if (req.exptime() == 0 || req.exptime() > exptime_) {
          auto mutReq = req;
          mutReq.exptime() = exptime_;
          return target_->route(mutReq);
        }
        return target_->route(req);
      }
    }
    return target_->route(req);
  }

  template <class Request>
  typename std::enable_if<
      !Request::hasExptime || DeleteLike<Request>::value,
      ReplyT<Request>>::type
  route(const Request& req) const {
    return target_->route(req);
  }

 private:
  const std::shared_ptr<RouteHandleIf> target_;
  const int32_t exptime_;
  const ModifyExptimeAction action_;
};

}}}  // facebook::memcache::mcrouter
