/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <folly/Format.h>

#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/routes/CarbonLookasideRoute.h"

namespace hellogoodbye {

class HelloGoodbyeCarbonLookasideHelper {
 public:
  static std::string name() {
    return "HelloGoodbyeCarbonLookasideHelper";
  }

  template <typename Request>
  bool cacheCandidate(const Request& /* unused */) {
    if (Request::hasKey) {
      return true;
    }
    return false;
  }

  template <typename Request>
  std::string buildKey(const Request& req) {
    if (Request::hasKey) {
      return req.key().fullKey().str();
    }
    return std::string();
  }
};

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeCarbonLookasideRoute(
    facebook::memcache::RouteHandleFactory<typename RouterInfo::RouteHandleIf>&
        factory,
    const folly::dynamic& json) {
  return facebook::memcache::mcrouter::
      createCarbonLookasideRoute<RouterInfo, HelloGoodbyeCarbonLookasideHelper>(
          factory, json);
}

} // namespace hellogoodbye
