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

#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include <folly/Conv.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Modifies exptime of a request.
 * If action == "set", applies a new expiration time.
 * If action == "min", applies a minimum of
 * (old expiration time, new expiration time).
 *
 * Note: 0 means infinite exptime
 */
class ModifyExptimeRoute {
 public:
  enum class Action {
    Set,
    Min
  };

  std::string routeName() const {
    return folly::sformat("modify-exptime|{}|exptime={}",
                          actionToString(action_), exptime_);
  }

  ModifyExptimeRoute(McrouterRouteHandlePtr target, int32_t exptime,
                     Action action)
    : target_(std::move(target)),
      exptime_(exptime),
      action_(action) {
    assert(action_ != Action::Min || exptime_ != 0);
  }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    t(*target_, req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type
  route(const Request& req, Operation) const {
    switch (action_) {
      case Action::Set: {
        auto mutReq = req.clone();
        mutReq.setExptime(exptime_);
        return target_->route(mutReq, Operation());
      }
      case Action::Min: {
        /* 0 means infinite exptime. Set minimum of request exptime, exptime. */
        if (req.exptime() == 0 || req.exptime() > exptime_) {
          auto mutReq = req.clone();
          mutReq.setExptime(exptime_);
          return target_->route(mutReq, Operation());
        }
        return target_->route(req, Operation());
      }
    }
    return target_->route(req, Operation());
  }

  static const char* actionToString(Action action);

 private:
  const McrouterRouteHandlePtr target_;
  const int32_t exptime_;
  const Action action_;
};

}}}  // facebook::memcache::mcrouter
