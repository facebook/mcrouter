/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <utility>

#include <folly/dynamic.h>

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/carbon/RoutingGroups.h"
#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/network/gen/MemcacheMessages.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

/**
 * Async logs a failed request. It assumes the required data is available in
 * the reply.
 */
template <class RouterInfo>
class AsynclogRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;

 public:
  std::string routeName() const {
    return "asynclog:" + asynclogName_;
  }

  AsynclogRoute(std::shared_ptr<RouteHandleIf> rh, std::string asynclogName)
      : rh_(std::move(rh)), asynclogName_(std::move(asynclogName)) {}

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*rh_, req);
  }

  memcache::McDeleteReply route(const memcache::McDeleteRequest& req) const {
    return fiber_local<RouterInfo>::runWithLocals([this, &req]() {
      fiber_local<RouterInfo>::setAsynclogName(asynclogName_);
      return rh_->route(req);
    });
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    return rh_->route(req);
  }

 private:
  const std::shared_ptr<RouteHandleIf> rh_;
  const std::string asynclogName_;
};

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeAsynclogRoute(
    typename RouterInfo::RouteHandlePtr rh,
    std::string asynclogName) {
  return makeRouteHandleWithInfo<RouterInfo, AsynclogRoute>(
      std::move(rh), std::move(asynclogName));
}

/**
 * @return target and asynclogName
 *         Caller may call makeAsynclogRoute afterwards.
 */
template <class RouterInfo>
std::pair<typename RouterInfo::RouteHandlePtr, std::string> parseAsynclogRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  std::string asynclogName;
  typename RouterInfo::RouteHandlePtr target;
  checkLogic(
      json.isObject() || json.isString(),
      "AsynclogRoute should be object or string");
  if (json.isString()) {
    asynclogName = json.getString();
    target = factory.create(json);
  } else { // object
    auto jname = json.get_ptr("name");
    checkLogic(
        jname && jname->isString(), "AsynclogRoute: required string name");
    auto jtarget = json.get_ptr("target");
    checkLogic(jtarget, "AsynclogRoute: target not found");
    asynclogName = jname->getString();
    target = factory.create(*jtarget);
  }
  return {std::move(target), std::move(asynclogName)};
}
} // mcrouter
} // memcache
} // facebook
