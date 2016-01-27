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

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "mcrouter/LeaseTokenMap.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/routes/AllSyncRoute.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/routes/BigValueRouteIf.h"
#include "mcrouter/routes/McOpList.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/RouteSelectorMap.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

class proxy_t;

/**
 * This is the top-most level of Mcrouter's RouteHandle tree.
 */
class ProxyRoute {
 public:
  static std::string routeName() { return "proxy"; }

  ProxyRoute(proxy_t* proxy, const RouteSelectorMap& routeSelectors);

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    t(*root_, req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    return root_->route(req);
  }

  McReply route(const McRequestWithMcOp<mc_op_lease_set>& req) {
    auto pair = queryLeaseTokenMap(req.leaseToken());
    if (pair.first) {
      stat_incr(proxy_->stats, redirected_lease_set_count_stat, 1);

      auto mutReq = req.clone();
      mutReq.setLeaseToken(pair.second);
      return fiber_local::runWithLocals(
        [destRoute = pair.first, &mutReq]() {
          fiber_local::addRequestClass(RequestClass::kFailover);
          return destRoute->route(mutReq);
        });
    }

    return root_->route(req);
  }

  McReply route(const McRequestWithMcOp<mc_op_flushall>& req) const {
    // route to all destinations in the config.
    return AllSyncRoute<McrouterRouteHandleIf>(getAllDestinations()).route(req);
  }

 private:
  proxy_t* proxy_;
  McrouterRouteHandlePtr root_;

  std::vector<McrouterRouteHandlePtr> getAllDestinations() const;
  // { destination, original token }
  std::pair<McrouterRouteHandlePtr, uint64_t> queryLeaseTokenMap(
      uint64_t leaseToken) const;
};

}}}  // facebook::memcache::mcrouter
