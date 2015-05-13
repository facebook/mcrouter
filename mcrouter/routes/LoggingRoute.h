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

#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Forwards requests to the child route, then logs the request and response.
 */
class LoggingRoute {
 public:
  static std::string routeName() {
    return "logging";
  }

  explicit LoggingRoute(McrouterRouteHandlePtr rh)
    : child_(std::move(rh)) {}

  LoggingRoute(RouteHandleFactory<McrouterRouteHandleIf>& factory,
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
  std::vector<McrouterRouteHandlePtr> couldRouteTo(
    const Request& req, Operation) const {

    return {child_};
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) {

    typename ReplyType<Operation,Request>::type reply;
    if (child_ == nullptr) {
      reply = NullRoute<McrouterRouteHandleIf>::route(req, Operation());
    } else {
      reply = child_->route(req, Operation());
    }

    LOG(INFO) << "request key: " << req.fullKey()
              << " response: " << mc_res_to_string(reply.result())
              << " responseLength: " << reply.value().length();
    return std::move(reply);
  }

 private:
  McrouterRouteHandlePtr child_;
};

}}}
