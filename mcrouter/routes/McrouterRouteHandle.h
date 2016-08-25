/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>

#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/network/CarbonMessageList.h"
#include "mcrouter/lib/RouteHandleIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

class McrouterRouteHandleIf;

template <typename Route>
class McrouterRouteHandle
    : public RouteHandle<Route, McrouterRouteHandleIf, CarbonRequestList> {
 public:
  template <typename... Args>
  explicit McrouterRouteHandle(Args&&... args)
      : RouteHandle<Route, McrouterRouteHandleIf, CarbonRequestList>(
            std::forward<Args>(args)...) {}
};

class McrouterRouteHandleIf
    : public RouteHandleIf<McrouterRouteHandleIf, CarbonRequestList> {
 public:
  template <class Route>
  using Impl = McrouterRouteHandle<Route>;
};

typedef std::shared_ptr<McrouterRouteHandleIf> McrouterRouteHandlePtr;

}}}  // facebook::memcache::mcrouter
