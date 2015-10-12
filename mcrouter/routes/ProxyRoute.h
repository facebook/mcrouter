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

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    t(*root_, req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {
    return root_->route(req, Operation());
  }

  template <class Request>
  typename ReplyType<McOperation<mc_op_lease_set>, Request>::type route(
    const Request& req, McOperation<mc_op_lease_set> op) {

    uint64_t originalLeaseToken;
    std::shared_ptr<ProxyDestination> destination;
    std::chrono::milliseconds timeout;
    if (queryLeaseTokenMap(req.leaseToken(), originalLeaseToken,
                           destination, timeout)) {
      auto mutReq = req.clone();
      mutReq.setLeaseToken(originalLeaseToken);

      stat_incr(proxy_->stats, redirected_lease_set_count_stat, 1);
      DestinationRequestCtx reqCtx(nowUs());
      return destination->send(mutReq, op, reqCtx, timeout);
    }

    return root_->route(req, op);
  }

  template <class Request>
  typename ReplyType<McOperation<mc_op_flushall>, Request>::type route(
      const Request& req, McOperation<mc_op_flushall> op) const {
    // route to all clients in the config.
    return AllSyncRoute<McrouterRouteHandleIf>(getAllDestinations())
        .route(req, op);
  }

 private:
  proxy_t* proxy_;
  McrouterRouteHandlePtr root_;

  std::vector<McrouterRouteHandlePtr> getAllDestinations() const;
  bool queryLeaseTokenMap(uint64_t leaseToken, uint64_t& originalLeaseToken,
                          std::shared_ptr<ProxyDestination>& destination,
                          std::chrono::milliseconds& timeout) const;
};

}}}  // facebook::memcache::mcrouter
