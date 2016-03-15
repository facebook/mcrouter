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
#include <string>
#include <vector>

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"

namespace facebook { namespace memcache {

/**
 * Returns the error reply for each request right away
 */
template <class RouteHandleIf>
struct ErrorRoute {
  std::string routeName() const {
    return "error" + (valueToSet_.empty() ? "" : "|" + valueToSet_);
  }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const { }

  explicit ErrorRoute(std::string valueToSet = "")
    : valueToSet_(std::move(valueToSet)) {}

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    return ReplyT<Request>(ErrorReply, valueToSet_);
  }

 private:
  const std::string valueToSet_;
};

}} // facebook::memcache
