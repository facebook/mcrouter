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

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/McOperation.h"

namespace facebook { namespace memcache {

/**
 * Forwards requests to the child route, then logs the request and response.
 */
template <class RouteHandleIf>
class LoggingRoute {
 public:
  static std::string routeName() {
    return "logging";
  }

  LoggingRoute(RouteHandleFactory<RouteHandleIf>& factory,
               const folly::dynamic& json) {
    if (json.isObject()) {
      if (json.count("target")) {
        child_ = factory.create(json["target"]);
      }
    } else if (json.isString()) {
      child_ = factory.create(json);
    }
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    return {child_};
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) {

    auto reply = child_->route(req, Operation());
    LOG(INFO) << "request key: " << req.fullKey()
              << " response: " << mc_res_to_string(reply.result())
              << " responseLength: " << reply.value().length();
    return std::move(reply);
  }

 private:
  std::shared_ptr<RouteHandleIf> child_;
};

}}
