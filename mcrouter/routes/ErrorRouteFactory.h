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

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/routes/ErrorRoute.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeErrorRoute(std::string valueToSet) {
  return makeRouteHandle<typename RouterInfo::RouteHandleIf, ErrorRoute>(
      std::move(valueToSet));
}

} // detail

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeErrorRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(
      json.isObject() || json.isString() || json.isNull(),
      "ErrorRoute: should be string or object");
  std::string response;
  if (json.isString()) {
    response = json.getString();
  } else if (json.isObject()) {
    if (auto jResponse = json.get_ptr("response")) {
      checkLogic(jResponse->isString(), "ErrorRoute: response is not a string");
      response = jResponse->getString();
    }
  }
  return detail::makeErrorRoute<RouterInfo>(std::move(response));
}
} // mcrouter
} // memcache
} // facebook
