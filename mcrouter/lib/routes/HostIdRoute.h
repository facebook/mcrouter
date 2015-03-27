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

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/McOperation.h"

namespace facebook { namespace memcache {

/**
 * Sends the request to single route from list based on hostid.
 */
template <class RouteHandleIf>
class HostIdRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;
  using StackContext = typename RouteHandleIf::StackContext;

  static std::string routeName() { return "hostid"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return { target_ };
  }

  HostIdRoute(RouteHandleFactory<RouteHandleIf>& factory,
              const folly::dynamic& json) {

    std::vector<std::shared_ptr<RouteHandleIf>> children;
    if (json.isObject()) {
      if (json.count("children")) {
        children = factory.createList(json["children"]);
      }
    } else {
      children = factory.createList(json);
    }

    checkLogic(!children.empty(), "HostId children is empty");

    target_ = children[globals::hostid() % children.size()];
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx,
    StackContext&& sctx) const {

    return target_->route(req, Operation(), ctx, std::move(sctx));
  }

 private:
  std::shared_ptr<RouteHandleIf> target_;
};

}}
