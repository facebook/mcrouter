/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "McExtraRouteHandleProvider.h"

#include "folly/Range.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/DefaultShadowPolicy.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeShadowRouteDefault(
  McrouterRouteHandlePtr normalRoute,
  McrouterShadowData shadowData,
  size_t normalIndex,
  DefaultShadowPolicy shadowPolicy);

McrouterRouteHandlePtr McExtraRouteHandleProvider::makeShadow(
  proxy_t* proxy,
  McrouterRouteHandlePtr destination,
  const McrouterShadowData& data,
  size_t indexInPool,
  folly::StringPiece shadowPolicy) {

  if (shadowPolicy == "default") {
    return makeShadowRouteDefault(destination, data, indexInPool,
                                  DefaultShadowPolicy());
  } else {
    throw std::logic_error("Invalid shadow policy: " + shadowPolicy.str());
  }
}

}}}  // facebook::memcache::mcrouter
