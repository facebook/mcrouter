/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/ProxyRequest.h"
#include "mcrouter/ProxyMcReply.h"
#include "mcrouter/RecordingContext.h"
#include "mcrouter/routes/McOpList.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/RouteHandleIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

class McrouterRouteHandleIf;

template <typename Route>
class McrouterRouteHandle :
      public RouteHandle<Route,
                         McrouterRouteHandleIf,
                         List<ProxyMcRequest, RecordingMcRequest>,
                         McOpList> {
 public:
  template<typename... Args>
  explicit McrouterRouteHandle(Args&&... args)
    : RouteHandle<Route,
                  McrouterRouteHandleIf,
                  List<ProxyMcRequest, RecordingMcRequest>,
                  McOpList>(
                    std::forward<Args>(args)...) {
  }
};

class McrouterRouteHandleIf :
      public RouteHandleIf<McrouterRouteHandleIf,
                           List<ProxyMcRequest, RecordingMcRequest>,
                           McOpList> {
 public:
  template <class Route>
  using Impl = McrouterRouteHandle<Route>;
};
typedef std::shared_ptr<McrouterRouteHandleIf> McrouterRouteHandlePtr;

}}}  // facebook::memcache::mcrouter
