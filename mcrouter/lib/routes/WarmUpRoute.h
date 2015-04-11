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

#include <folly/dynamic.h>
#include <folly/experimental/fibers/FiberManager.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/Reply.h"

namespace facebook { namespace memcache {

/**
 * This route handle allows for substantial changes in the number of boxes in
 * a pool without increasing the miss rate and, subsequently, the load on the
 * underlying storage or service.
 *
 * get: send the request to "cold" route handle and in case of a miss,
 *     fetch data from the "warm" route handle. If "warm" returns a hit,
 *     the response is then forwarded to the client and an asynchronous 'add'
 *     request updates the value in the "cold" route handle.
 * gets: send the request to "cold" route handle and in case of a miss,
 *     fetch data from the "warm" route handle with simple 'get' request.
 *     If "warm" returns a hit, synchronously try to add value to "cold"
 *     using 'add' operation and send original 'gets' request to "cold" one
 *     more time.
 * lease get: send the request to "cold" route and in case of a miss
 *     (not hot miss!) fetch data from the "warm" using simple 'get' request.
 *     If "warm" returns a hit, the response is forwarded to the client and
 *     an asynchronous lease set updates the value in the cold route handle.
 * metaget: send the request to "cold" route and in case of a miss, send
 *     request to "warm".
 * set/delete/incr/decr/etc.: send to the "cold" route, do not modify "warm".
 *     Client is responsible for "warm" consistency.
 *
 * Expiration time (TTL) for automatic warm -> cold update requests is
 * configured with "exptime" field. If the field is not present, exptime is
 * fetched from "warm" on every update operation with additional 'metaget'
 * request.
 * NOTE: Make sure memcached supports 'metaget' before omitting "exptime" field.
 */
template <class RouteHandleIf>
class WarmUpRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;

  static std::string routeName() { return "warm-up"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return {cold_, warm_};
  }

  WarmUpRoute(std::shared_ptr<RouteHandleIf> warm,
              std::shared_ptr<RouteHandleIf> cold,
              uint32_t exptime)
  : warm_(std::move(warm)),
    cold_(std::move(cold)),
    exptime_(exptime) {

    assert(warm_ != nullptr);
    assert(cold_ != nullptr);
  }

  WarmUpRoute(RouteHandleFactory<RouteHandleIf>& factory,
              const folly::dynamic& json) {

    checkLogic(json.isObject(), "WarmUpRoute should be object");
    checkLogic(json.count("cold"), "WarmUpRoute: no cold route");
    checkLogic(json.count("warm"), "WarmUpRoute: no warm route");
    if (json.count("exptime")) {
      checkLogic(json["exptime"].isInt(),
                 "WarmUpRoute: exptime is not an integer");
      exptime_ = json["exptime"].getInt();
    }

    cold_ = factory.create(json["cold"]);
    warm_ = factory.create(json["warm"]);
  }

  ////////////////////////////////mc_op_get/////////////////////////////////////
  template <class Request>
  typename ReplyType<McOperation<mc_op_get>, Request>::type
  route(const Request& req, McOperation<mc_op_get> op, const ContextPtr& ctx) {
    auto coldReply = cold_->route(req, op, ctx);
    if (coldReply.isHit()) {
      return coldReply;
    }

    /* else */
    auto warmReply = warm_->route(req, op, ctx);
#ifdef __clang__
#pragma clang diagnostic push // ignore generalized lambda capture warning
#pragma clang diagnostic ignored "-Wc++1y-extensions"
#endif
    uint32_t exptime;
    if (warmReply.isHit() && getExptimeForCold(req, exptime, ctx)) {
      folly::fibers::addTask([
          cold = cold_,
          addReq = coldUpdateFromWarm(req, warmReply, exptime),
          ctx]() {
        cold->route(addReq, McOperation<mc_op_add>(), ctx);
      });
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    return warmReply;
  }

  ///////////////////////////////mc_op_metaget//////////////////////////////////
  template <class Request>
  typename ReplyType<McOperation<mc_op_metaget>, Request>::type
  route(const Request& req, McOperation<mc_op_metaget> op,
        const ContextPtr& ctx) {
    auto coldReply = cold_->route(req, op, ctx);
    if (coldReply.isHit()) {
      return coldReply;
    }
    return warm_->route(req, op, ctx);
  }

  /////////////////////////////mc_op_lease_get//////////////////////////////////
  template <class Request>
  typename ReplyType<McOperation<mc_op_lease_get>, Request>::type
  route(const Request& req, McOperation<mc_op_lease_get> op,
        const ContextPtr& ctx) {
    auto coldReply = cold_->route(req, op, ctx);
    if (coldReply.isHit() || coldReply.isHotMiss()) {
      // in case of a hot miss somebody else will set the value
      return coldReply;
    }

    // miss with lease token from cold route: send simple get to warm route
    auto warmReply = warm_->route(req, McOperation<mc_op_get>(), ctx);
#ifdef __clang__
#pragma clang diagnostic push // ignore generalized lambda capture warning
#pragma clang diagnostic ignored "-Wc++1y-extensions"
#endif
    uint32_t exptime;
    if (warmReply.isHit() && getExptimeForCold(req, exptime, ctx)) {
      // update cold route with lease set
      auto setReq = coldUpdateFromWarm(req, warmReply, exptime);
      setReq.setLeaseToken(coldReply.leaseToken());

      folly::fibers::addTask([cold = cold_, req = std::move(setReq), ctx]() {
        cold->route(req, McOperation<mc_op_lease_set>(), ctx);
      });
      return warmReply;
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    return coldReply;
  }

  ////////////////////////////////mc_op_gets////////////////////////////////////
  template <class Request>
  typename ReplyType<McOperation<mc_op_gets>, Request>::type
  route(const Request& req, McOperation<mc_op_gets> op,
        const ContextPtr& ctx) {
    auto coldReply = cold_->route(req, op, ctx);
    if (coldReply.isHit()) {
      return coldReply;
    }

    // miss: send simple get to warm route
    auto warmReply = warm_->route(req, McOperation<mc_op_get>(), ctx);
    uint32_t exptime;
    if (warmReply.isHit() && getExptimeForCold(req, exptime, ctx)) {
      // update cold route if we have the value
      auto addReq = coldUpdateFromWarm(req, warmReply, exptime);
      cold_->route(addReq, McOperation<mc_op_add>(), ctx);
      // and grab cas token again
      return cold_->route(req, op, ctx);
    }
    return coldReply;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    const ContextPtr& ctx) const {
    // client is responsible for consistency of warm route, do not replicate
    // any update/delete operations
    return cold_->route(req, Operation(), ctx);
  }

 private:
  std::shared_ptr<RouteHandleIf> warm_;
  std::shared_ptr<RouteHandleIf> cold_;
  folly::Optional<uint32_t> exptime_;

  template <class Request, class Reply>
  static Request coldUpdateFromWarm(const Request& origReq,
                                    const Reply& reply,
                                    uint32_t exptime) {
    auto req = origReq.clone();
    folly::IOBuf cloned;
    reply.value().cloneInto(cloned);
    req.setValue(std::move(cloned));
    req.setFlags(reply.flags());
    req.setExptime(exptime);
    return std::move(req);
  }

  template <class Request>
  bool getExptimeForCold(const Request& req, uint32_t& exptime,
                         const ContextPtr& ctx) {
    if (exptime_.hasValue()) {
      exptime = *exptime_;
      return true;
    }
    auto warmMeta = warm_->route(req, McOperation<mc_op_metaget>(), ctx);
    if (warmMeta.isHit()) {
      exptime = warmMeta.exptime();
      if (exptime != 0) {
        auto curTime = time(nullptr);
        if (curTime >= exptime) {
          return false;
        }
        exptime -= curTime;
      }
      return true;
    }
    return false;
  }
};

}}
