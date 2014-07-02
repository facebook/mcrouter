/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mcrouter/config.h"
#include "mcrouter/routes/McOpList.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/RouteHandleMap.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/routes/ErrorRoute.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * This is the top-most level of Mcrouter's RouteHandle tree.
 */
class ProxyRoute {
 private:
  ProxyMcReply dispatchMcMsgHelper(
    const McMsgRef& msg,
    std::shared_ptr<ProxyRequestContext> ctx,
    McOpList::Item<0>) const {

    throw std::runtime_error("dispatch for requested op not implemented");
  }

  template <int op_id>
  ProxyMcReply dispatchMcMsgHelper(
    const McMsgRef& msg,
    std::shared_ptr<ProxyRequestContext> ctx,
    McOpList::Item<op_id>) const {

    if (msg->op == McOpList::Item<op_id>::op::mc_op) {
      return route(ProxyMcRequest(ctx, msg.clone()),
                   typename McOpList::Item<op_id>::op());
    }

    return dispatchMcMsgHelper(msg, std::move(ctx), McOpList::Item<op_id-1>());
  }

 public:
  static std::string routeName() { return "proxy"; }

  ProxyRoute(proxy_t* proxy, const RouteSelectorMap& routeSelectors)
      : proxy_(proxy),
        routeHandleMap_(routeSelectors, proxy_->default_route,
                        proxy_->opts.send_invalid_route_to_default) {
  }

  ProxyMcReply dispatchMcMsg(const McMsgRef& msg,
                             std::shared_ptr<ProxyRequestContext> ctx) const {
    return dispatchMcMsgHelper(msg, std::move(ctx), McOpList::LastItem());
  }

  template <class Operation, class Request>
  std::vector<McrouterRouteHandlePtr> couldRouteTo(
    const Request& req, Operation) const {

    return routeHandleMap_.getTargetsForKey(req.routingPrefix(),
                                            req.routingKey());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    /* If we have to send to more than one prefix,
       wait for the first in the list to reply and let others
       run in the background.

       This is a good default for /star/star/ requests.

       TODO: honor rep_key_policies here */
    auto rh = routeHandleMap_.getTargetsForKey(req.routingPrefix(),
                                               req.routingKey());
    return routeImpl(rh, req, Operation());
  }

  ProxyMcReply route(
    const ProxyMcRequest& req, McOperation<mc_op_stats>) const {

    auto msg = McMsgRef::moveRef(stats_reply(proxy_, req.fullKey()));

    return ProxyMcReply(msg->result, std::move(msg));
  }

  /**
   * Returns the route handles for the given pool name.
   * Returns nullptr if pool name is not found.
   */
  McrouterRouteHandlePtr getRouteHandleForProxyPool(
    const std::string& poolName) {

    return routeHandleMap_.getRouteHandleForProxyPool(poolName);
  }

 private:
  proxy_t* proxy_;
  RouteHandleMap routeHandleMap_;

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation, typename GetLike<Operation>::Type = 0)
    const {

    auto reply = doRoute(rh, req, Operation());
    if (!reply.isError() || rh.empty()) {
      /* rh.empty() case: for backwards compatibility,
         always surface invalid routing errors */
      return reply;
    }

    if (!preprocessGetErrors(proxy_->opts, reply) &&
        proxy_->opts.miss_on_get_errors) {
      reply = NullRoute<McrouterRouteHandleIf>::route(req, Operation());
    }

    return reply;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation, typename ArithmeticLike<Operation>::Type = 0)
    const {

    auto reply = doRoute(rh, req, Operation());
    if (reply.isError()) {
      return NullRoute<McrouterRouteHandleIf>::route(req, Operation());
    }
    return reply;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type routeImpl(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation,
    OtherThanT(Operation, GetLike<>, ArithmeticLike<>) = 0)
    const {

    return doRoute(rh, req, Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type doRoute(
    const std::vector<McrouterRouteHandlePtr>& rh,
    const Request& req, Operation) const {

    if (rh.empty()) {
      return ErrorRoute<McrouterRouteHandleIf>().route(req, Operation());
    }

    if (rh.size() > 1) {
      auto reqCopy = std::make_shared<Request>(req.clone());
      for (size_t i = 1; i < rh.size(); ++i) {
        auto r = rh[i];
        proxy_->fiberManager.addTask(
          [r, reqCopy]() {
            r->route(*reqCopy, Operation());
          });
      }
    }
    return rh[0]->route(req, Operation());
  }
};

}}}  // facebook::memcache::mcrouter
