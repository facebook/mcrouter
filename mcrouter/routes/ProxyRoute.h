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

#include <memory>
#include <string>
#include <vector>

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/routes/AllSyncRoute.h"
#include "mcrouter/routes/BigValueRouteIf.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/RouteSelectorMap.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouterInfo>
class Proxy;

/**
 * This is the top-most level of Mcrouter's RouteHandle tree.
 */
template <class RouterInfo>
class ProxyRoute {
 public:
  static std::string routeName() {
    return "proxy";
  }

  ProxyRoute(
      Proxy<RouterInfo>* proxy,
      const RouteSelectorMap<typename RouterInfo::RouteHandleIf>&
          routeSelectors);

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<typename RouterInfo::RouteHandleIf>& t) const {
    t(*root_, req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    return root_->route(req);
  }

  McFlushAllReply route(const McFlushAllRequest& req) const {
    // route to all destinations in the config.
    return AllSyncRoute<typename RouterInfo::RouteHandleIf>(
               getAllDestinations())
        .route(req);
  }

  // Routing disabled for the commands bellow.
  // TODO(@aap): Remove after refactoring Proxy.
  McVersionReply route(const McVersionRequest&) const {
    throw std::runtime_error("Routing version command is not supported.");
  }
  McStatsReply route(const McStatsRequest&) const {
    throw std::runtime_error("Routing stats command is not supported.");
  }
  McShutdownReply route(const McShutdownRequest&) const {
    throw std::runtime_error("Routing shutdown command is not supported.");
  }
  McQuitReply route(const McQuitRequest& req) const {
    throw std::runtime_error("Routing quit command is not supported.");
  }
  McExecReply route(const McExecRequest& req) const {
    throw std::runtime_error("Routing exec command is not supported.");
  }

  void traverse(
      const McVersionRequest&,
      const RouteHandleTraverser<typename RouterInfo::RouteHandleIf>& t) const {
    throw std::runtime_error("Routing version command is not supported.");
  }
  void traverse(
      const McStatsRequest&,
      const RouteHandleTraverser<typename RouterInfo::RouteHandleIf>& t) const {
    throw std::runtime_error("Routing stats command is not supported.");
  }
  void traverse(
      const McShutdownRequest&,
      const RouteHandleTraverser<typename RouterInfo::RouteHandleIf>& t) const {
    throw std::runtime_error("Routing shutdown command is not supported.");
  }
  void traverse(
      const McQuitRequest& req,
      const RouteHandleTraverser<typename RouterInfo::RouteHandleIf>& t) const {
    throw std::runtime_error("Routing quit command is not supported.");
  }
  void traverse(
      const McExecRequest& req,
      const RouteHandleTraverser<typename RouterInfo::RouteHandleIf>& t) const {
    throw std::runtime_error("Routing exec command is not supported.");
  }

 private:
  Proxy<RouterInfo>* proxy_;
  std::shared_ptr<typename RouterInfo::RouteHandleIf> root_;

  std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>>
  getAllDestinations() const;
};
}
}
} // facebook::memcache::mcrouter

#include "ProxyRoute-inl.h"
