/*
 *  Copyright (c) 2015-present, Facebook, Inc.
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
#include <folly/Format.h>
#include <folly/Range.h>
#include <folly/dynamic.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/carbon/RoutingGroups.h"
#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

enum class ModifyExptimeAction { Set, Min };

namespace detail {

inline ModifyExptimeAction stringToAction(folly::StringPiece s) {
  if (s == "set") {
    return ModifyExptimeAction::Set;
  } else if (s == "min") {
    return ModifyExptimeAction::Min;
  }
  checkLogic(false, "ModifyExptimeRoute: action should be 'set' or 'min'");
  return ModifyExptimeAction::Set;
}

inline const char* actionToString(ModifyExptimeAction action) {
  switch (action) {
    case ModifyExptimeAction::Set:
      return "set";
    case ModifyExptimeAction::Min:
      return "min";
  }
  assert(false);
  return "";
}

} // detail

/**
 * Modifies exptime of a request.
 * Note that exptime is not modified for delete requests
 * If action == "set", applies a new expiration time.
 * If action == "min", applies a minimum of
 * (old expiration time, new expiration time).
 *
 * Note: 0 means infinite exptime.
 */
template <class RouteHandleIf>
class ModifyExptimeRoute {
 public:
  std::string routeName() const {
    return folly::sformat(
        "modify-exptime|{}|exptime={}",
        detail::actionToString(action_),
        exptime_);
  }

  ModifyExptimeRoute(
      std::shared_ptr<RouteHandleIf> target,
      int32_t exptime,
      ModifyExptimeAction action)
      : target_(std::move(target)), exptime_(exptime), action_(action) {
    assert(action_ != ModifyExptimeAction::Min || exptime_ != 0);
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*target_, req);
  }

  template <class Request>
  typename std::enable_if<
      Request::hasExptime &&
          carbon::OtherThan<Request, carbon::DeleteLike<>>::value,
      ReplyT<Request>>::type
  route(const Request& req) const {
    switch (action_) {
      case ModifyExptimeAction::Set: {
        auto mutReq = req;
        mutReq.exptime() = exptime_;
        return target_->route(mutReq);
      }
      case ModifyExptimeAction::Min: {
        /* 0 means infinite exptime. Set minimum of request exptime, exptime. */
        if (req.exptime() == 0 || req.exptime() > exptime_) {
          auto mutReq = req;
          mutReq.exptime() = exptime_;
          return target_->route(mutReq);
        }
        return target_->route(req);
      }
    }
    return target_->route(req);
  }

  template <class Request>
  typename std::enable_if<
      !Request::hasExptime || carbon::DeleteLike<Request>::value,
      ReplyT<Request>>::type
  route(const Request& req) const {
    return target_->route(req);
  }

 private:
  const std::shared_ptr<RouteHandleIf> target_;
  const int32_t exptime_;
  const ModifyExptimeAction action_;
};

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeModifyExptimeRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "ModifyExptimeRoute: should be an object");
  auto jtarget = json.get_ptr("target");
  checkLogic(jtarget, "ModifyExptimeRoute: no target");
  auto target = factory.create(*jtarget);
  auto jexptime = json.get_ptr("exptime");
  checkLogic(jexptime, "ModifyExptimeRoute: no exptime");
  checkLogic(jexptime->isInt(), "ModifyExptimeRoute: exptime is not an int");
  auto exptime = jexptime->getInt();

  ModifyExptimeAction action{ModifyExptimeAction::Set};
  if (auto jaction = json.get_ptr("action")) {
    checkLogic(
        jaction->isString(), "ModifyExptimeRoute: action is not a string");
    action = detail::stringToAction(jaction->getString());
  }

  // 0 means infinite exptime
  if (action == ModifyExptimeAction::Min && exptime == 0) {
    return target;
  }

  return makeRouteHandle<
      typename RouterInfo::RouteHandleIf,
      ModifyExptimeRoute>(std::move(target), exptime, action);
}
} // mcrouter
} // memcache
} // facebook
