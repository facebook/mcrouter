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
#include <string>
#include <vector>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/routes/AllSyncRoute.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/routes/BigValueRouteIf.h"
#include "mcrouter/routes/McOpList.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/RootRoute.h"
#include "mcrouter/routes/RouteSelectorMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeBigValueRoute(McrouterRouteHandlePtr ch,
                                         BigValueRouteOptions options);

McrouterRouteHandlePtr
makeDestinationRoute(std::shared_ptr<const ProxyClientCommon> client,
                     std::shared_ptr<ProxyDestination> destination);

McrouterRouteHandlePtr makeLoggingRoute(McrouterRouteHandlePtr rh);

/**
 * This is the top-most level of Mcrouter's RouteHandle tree.
 */
class ProxyRoute {
 private:
  McReply dispatchMcMsgHelper(
    McMsgRef&& msg,
    McOpList::Item<0>) const {

    throw std::runtime_error("dispatch for requested op not implemented");
  }

  template <int op_id>
  McReply dispatchMcMsgHelper(
    McMsgRef&& msg,
    McOpList::Item<op_id>) const {

    if (msg->op == McOpList::Item<op_id>::op::mc_op) {
      return route(McRequest(std::move(msg)),
                   typename McOpList::Item<op_id>::op());
    }

    return dispatchMcMsgHelper(std::move(msg), McOpList::Item<op_id-1>());
  }

 public:
  static std::string routeName() { return "proxy"; }

  ProxyRoute(proxy_t* proxy, const RouteSelectorMap& routeSelectors)
      : proxy_(proxy) {
    root_ = std::make_shared<McrouterRouteHandle<RootRoute>>(
      proxy_, routeSelectors);
    if (proxy_->router().opts().big_value_split_threshold != 0) {
      BigValueRouteOptions options(
        proxy_->router().opts().big_value_split_threshold,
        proxy_->router().opts().big_value_batch_size);
      root_ = makeBigValueRoute(std::move(root_), std::move(options));
    }
    if (proxy_->router().opts().enable_logging_route) {
      root_ = makeLoggingRoute(std::move(root_));
    }
  }

  McReply dispatchMcMsg(McMsgRef&& msg) const {
    return dispatchMcMsgHelper(std::move(msg), McOpList::LastItem());
  }

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
  typename ReplyType<McOperation<mc_op_flushall>, Request>::type route(
    const Request& req, McOperation<mc_op_flushall> op) const {

    // route to all clients in the config
    std::vector<McrouterRouteHandlePtr> rh;
    auto clients = proxy_->getConfig()->getClients();
    for (auto& client : clients) {
      auto dest = proxy_->destinationMap->fetch(*client);
      rh.push_back(makeDestinationRoute(std::move(client), std::move(dest)));
    }
    return
      AllSyncRoute<McrouterRouteHandleIf>(std::move(rh)).route(req, op);
  }

 private:
  proxy_t* proxy_;
  McrouterRouteHandlePtr root_;
};

}}}  // facebook::memcache::mcrouter
