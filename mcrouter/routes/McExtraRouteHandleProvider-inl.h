/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McExtraRouteHandleProvider.h"

#include <folly/Range.h>

#include "mcrouter/ProxyBase.h"
#include "mcrouter/routes/DefaultShadowPolicy.h"
#include "mcrouter/routes/FailoverRoute.h"
#include "mcrouter/routes/ShadowRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf>
McExtraRouteHandleProvider<RouterInfo>::makeShadow(
    ProxyBase&,
    std::shared_ptr<typename RouterInfo::RouteHandleIf> destination,
    ShadowData<RouterInfo> data,
    folly::StringPiece shadowPolicy) {
  if (shadowPolicy == "default") {
    return makeShadowRouteDefault<RouterInfo>(
        std::move(destination), std::move(data), DefaultShadowPolicy());
  } else {
    throw std::logic_error("Invalid shadow policy: " + shadowPolicy.str());
  }
}

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf>
McExtraRouteHandleProvider<RouterInfo>::makeFailoverRoute(
    const folly::dynamic& json,
    std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> children) {
  return makeFailoverRouteDefault<RouterInfo, FailoverRoute>(
      json, std::move(children));
}

template <class RouterInfo>
std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>>
McExtraRouteHandleProvider<RouterInfo>::tryCreate(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    folly::StringPiece type,
    const folly::dynamic& json) {
  return {};
}

} // mcrouter
} // memcache
} // facebook
