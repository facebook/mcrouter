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

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"

namespace facebook { namespace memcache {

/**
 * Returns the default reply for each request right away
 */
template <class RouteHandleIf>
struct NullRoute {
  static std::string routeName() {
    return "null";
  }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const { }

  template <class Request>
  static ReplyT<Request> route(const Request& req) {
    return ReplyT<Request>(DefaultReply, req);
  }
};

}} // facebook::memcache
