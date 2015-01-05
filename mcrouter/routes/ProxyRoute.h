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

#include "mcrouter/proxy.h"
#include "mcrouter/routes/BigValueRouteIf.h"
#include "mcrouter/routes/McOpList.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/RootRoute.h"
#include "mcrouter/routes/RouteSelectorMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeBigValueRoute(McrouterRouteHandlePtr ch,
                                         BigValueRouteOptions options);

/**
 * This is the top-most level of Mcrouter's RouteHandle tree.
 */
class ProxyRoute {
 private:
  ProxyMcReply dispatchMcMsgHelper(
    McMsgRef&& msg,
    std::shared_ptr<GenericProxyRequestContext> ctx,
    McOpList::Item<0>) const {

    throw std::runtime_error("dispatch for requested op not implemented");
  }

  template <int op_id>
  ProxyMcReply dispatchMcMsgHelper(
    McMsgRef&& msg,
    std::shared_ptr<GenericProxyRequestContext> ctx,
    McOpList::Item<op_id>) const {

    if (msg->op == McOpList::Item<op_id>::op::mc_op) {
      return route(ProxyMcRequest(std::move(ctx), std::move(msg)),
                   typename McOpList::Item<op_id>::op());
    }

    return dispatchMcMsgHelper(std::move(msg), std::move(ctx),
                               McOpList::Item<op_id-1>());
  }

 public:
  static std::string routeName() { return "proxy"; }

  ProxyRoute(proxy_t* proxy, const RouteSelectorMap& routeSelectors)
      : proxy_(proxy) {
    root_ = std::make_shared<McrouterRouteHandle<RootRoute>>(
      proxy_, routeSelectors);
    if (proxy_->opts.big_value_split_threshold != 0) {
      BigValueRouteOptions options(proxy_->opts.big_value_split_threshold);
      root_ = makeBigValueRoute(std::move(root_), std::move(options));
    }
  }

  ProxyMcReply dispatchMcMsg(
    McMsgRef&& msg,
    std::shared_ptr<GenericProxyRequestContext> ctx) const {

    return dispatchMcMsgHelper(std::move(msg), std::move(ctx),
                               McOpList::LastItem());
  }

  template <class Operation, class Request>
  std::vector<McrouterRouteHandlePtr> couldRouteTo(
    const Request& req, Operation) const {

    return { root_ };
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {
    return root_->route(req, Operation());
  }

 private:
  proxy_t* proxy_;
  McrouterRouteHandlePtr root_;
};

}}}  // facebook::memcache::mcrouter
