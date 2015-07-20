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
#include <folly/Optional.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Modifies exptime of a request.
 */
class ModifyExptimeRoute {
 public:
  std::string routeName() const {
    return folly::sformat("modify-exptime|exptime={}", exptime_);
  }

  ModifyExptimeRoute(McrouterRouteHandlePtr target, int32_t exptime)
    : target_(std::move(target)),
      exptime_(exptime) {
  }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    auto mutReq = req.clone();
    mutReq.setExptime(exptime_);
    t(*target_, mutReq, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type
  route(const Request& req, Operation) const {
    auto mutReq = req.clone();
    mutReq.setExptime(exptime_);
    return target_->route(mutReq, Operation());
  }

 private:
  const McrouterRouteHandlePtr target_;
  const int32_t exptime_;
};

}}}  // facebook::memcache::mcrouter
