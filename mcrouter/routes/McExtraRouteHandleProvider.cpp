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

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeShadowRouteDefault(
  McrouterRouteHandlePtr normalRoute,
  McrouterShadowData shadowData,
  DefaultShadowPolicy shadowPolicy);

McrouterRouteHandlePtr McExtraRouteHandleProvider::makeShadow(
    ProxyBase&,
    McrouterRouteHandlePtr destination,
    McrouterShadowData data,
    folly::StringPiece shadowPolicy) {
  if (shadowPolicy == "default") {
    return makeShadowRouteDefault(std::move(destination), std::move(data),
                                  DefaultShadowPolicy());
  } else {
    throw std::logic_error("Invalid shadow policy: " + shadowPolicy.str());
  }
}

McrouterRouteHandlePtr McExtraRouteHandleProvider::makeFailoverRoute(
    const folly::dynamic& json,
    std::vector<McrouterRouteHandlePtr> children) {
  return makeFailoverRouteDefault<FailoverRoute>(json, std::move(children));
}

std::vector<McrouterRouteHandlePtr> McExtraRouteHandleProvider::tryCreate(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    folly::StringPiece type,
    const folly::dynamic& json) {
  return {};
}

}}}  // facebook::memcache::mcrouter
