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

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/RouteHandleIf.h"
#include "mcrouter/McrouterStackContext.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/McOpList.h"

namespace facebook { namespace memcache { namespace mcrouter {

class McrouterRouteHandleIf;

template <typename Route>
class McrouterRouteHandle :
      public RouteHandle<Route,
                         McrouterRouteHandleIf,
                         ProxyRequestContext,
                         McrouterStackContext,
                         List<McRequest>,
                         McOpList> {
 public:
  template<typename... Args>
  explicit McrouterRouteHandle(Args&&... args)
    : RouteHandle<Route,
                  McrouterRouteHandleIf,
                  ProxyRequestContext,
                  McrouterStackContext,
                  List<McRequest>,
                  McOpList>(
                    std::forward<Args>(args)...) {
  }
};

class McrouterRouteHandleIf :
      public RouteHandleIf<McrouterRouteHandleIf,
                           ProxyRequestContext,
                           McrouterStackContext,
                           List<McRequest>,
                           McOpList> {
 public:
  template <class Route>
  using Impl = McrouterRouteHandle<Route>;

  using RouteHandleIf<McrouterRouteHandleIf,
                      ProxyRequestContext,
                      McrouterStackContext,
                      List<McRequest>,
                      McOpList>::ContextPtr;
  using RouteHandleIf<McrouterRouteHandleIf,
                      ProxyRequestContext,
                      McrouterStackContext,
                      List<McRequest>,
                      McOpList>::StackContext;
};

typedef std::shared_ptr<McrouterRouteHandleIf> McrouterRouteHandlePtr;

}}}  // facebook::memcache::mcrouter
