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

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<RouteHandleIf>& t) const { }

  explicit ErrorRoute(std::string valueToSet = "")
    : valueToSet_(std::move(valueToSet)) {}

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) {

    typedef typename ReplyType<Operation, Request>::type Reply;

    return Reply(ErrorReply, valueToSet_);
  }

 private:
  const std::string valueToSet_;
};

}}
