/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "folly/dynamic.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/McOperation.h"

namespace facebook { namespace memcache {

/**
 * Returns the error reply for each request right away
 */
template <class RouteHandleIf>
struct ErrorRoute {
  static std::string routeName() {
    return "error";
  }

  template <class Operation, class Request>
  static std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) {

    return {};
  }

  ErrorRoute() {}

  explicit ErrorRoute(const folly::dynamic& json) {
    checkLogic(json.isString() || json.isNull(),
               "ErrorRoute: response should be string or empty");

    if (json.isString()) {
      valueToSet_ = json.asString().toStdString();
    }
  }

  explicit ErrorRoute(std::string valueToSet)
    : valueToSet_(std::move(valueToSet)) {}

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) {

    typedef typename ReplyType<Operation, Request>::type Reply;

    return Reply::errorReply(valueToSet_);
  }

 private:
  std::string valueToSet_;
};

}}
