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
#include <string>

#include <folly/experimental/fibers/FiberManager.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"

namespace facebook { namespace memcache { namespace mcrouter {

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
 * configured with "exptime" field. If the field is not present and
 * "enable_metaget" is true, exptime is fetched from "warm" on every update
 * operation with additional 'metaget' request.
 */
template <class RouteHandleIf>
class WarmUpRoute {
 public:
  static std::string routeName() { return "warm-up"; }

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*cold_, req);
    t(*warm_, req);
  }

  WarmUpRoute(std::shared_ptr<RouteHandleIf> warm,
              std::shared_ptr<RouteHandleIf> cold,
              folly::Optional<uint32_t> exptime)
  : warm_(std::move(warm)),
    cold_(std::move(cold)),
    exptime_(std::move(exptime)) {

    assert(warm_ != nullptr);
    assert(cold_ != nullptr);
  }

  //////////////////////////////// get /////////////////////////////////////
  McReply route(const McRequestWithMcOp<mc_op_get>& req) {
    auto coldReply = cold_->route(req);
    if (coldReply.isHit()) {
      return coldReply;
    }

    /* else */
    auto warmReply = warm_->route(req);
    uint32_t exptime;
    if (warmReply.isHit() && getExptimeForCold(req, exptime)) {
      folly::fibers::addTask([
          cold = cold_,
          addReq = coldUpdateFromWarm<McRequestWithMcOp<mc_op_add>>(
            req, warmReply, exptime)]() {
        cold->route(addReq);
      });
    }
    return warmReply;
  }

  TypedThriftReply<cpp2::McGetReply> route(
      const TypedThriftRequest<cpp2::McGetRequest>& req) {

    auto coldReply = cold_->route(req);
    if (coldReply.isHit()) {
      return coldReply;
    }

    /* else */
    auto warmReply = warm_->route(req);
    uint32_t exptime;
    if (warmReply.isHit() && getExptimeForCold(req, exptime)) {
      folly::fibers::addTask([
          cold = cold_,
          addReq = coldUpdateFromWarm<TypedThriftRequest<cpp2::McAddRequest>>(
            req, warmReply, exptime)]() {
        cold->route(addReq);
      });
    }
    return warmReply;
  }

  ///////////////////////////////metaget//////////////////////////////////
  McReply route(const McRequestWithMcOp<mc_op_metaget>& req) {
    auto coldReply = cold_->route(req);
    if (coldReply.isHit()) {
      return coldReply;
    }
    return warm_->route(req);
  }

  TypedThriftReply<cpp2::McMetagetReply> route(
      const TypedThriftRequest<cpp2::McMetagetRequest>& req) {

    auto coldReply = cold_->route(req);
    if (coldReply.isHit()) {
      return coldReply;
    }
    return warm_->route(req);
  }

  /////////////////////////////lease_get//////////////////////////////////
  McReply route(const McRequestWithMcOp<mc_op_lease_get>& req) {
    auto coldReply = cold_->route(req);
    if (coldReply.isHit() || coldReply.isHotMiss()) {
      // in case of a hot miss somebody else will set the value
      return coldReply;
    }

    // miss with lease token from cold route: send simple get to warm route
    McRequestWithMcOp<mc_op_get> reqOpGet(req.clone());
    auto warmReply = warm_->route(reqOpGet);
    uint32_t exptime;
    if (warmReply.isHit() && getExptimeForCold(reqOpGet, exptime)) {
      // update cold route with lease set
      auto setReq = coldUpdateFromWarm<McRequestWithMcOp<mc_op_lease_set>>(
          reqOpGet, warmReply, exptime);
      setReq.setLeaseToken(coldReply.leaseToken());

      folly::fibers::addTask([cold = cold_, req = std::move(setReq)]() {
        cold->route(req);
      });
      return warmReply;
    }
    return coldReply;
  }

  TypedThriftReply<cpp2::McLeaseGetReply> route(
      const TypedThriftRequest<cpp2::McLeaseGetRequest>& req) {

    auto coldReply = cold_->route(req);
    if (coldReply.isHit() || coldReply.isHotMiss()) {
      // in case of a hot miss somebody else will set the value
      return coldReply;
    }

    // miss with lease token from cold route: send simple get to warm route
    TypedThriftRequest<cpp2::McGetRequest> reqOpGet(req.fullKey());
    auto warmReply = warm_->route(reqOpGet);
    uint32_t exptime;
    if (warmReply.isHit() && getExptimeForCold(reqOpGet, exptime)) {
      // update cold route with lease set
      auto setReq =
        coldUpdateFromWarm<TypedThriftRequest<cpp2::McLeaseSetRequest>>(
            reqOpGet, warmReply, exptime);
      const auto leaseToken =
        coldReply->get_leaseToken() ? *coldReply->get_leaseToken() : 0;
      setReq->set_leaseToken(leaseToken);

      folly::fibers::addTask([cold = cold_, req = std::move(setReq)]() {
        cold->route(req);
      });
      // On hit, no need to copy appSpecificErrorCode or message
      TypedThriftReply<cpp2::McLeaseGetReply> reply(warmReply.result());
      reply.setFlags(warmReply.flags());
      if (warmReply->get_value()) {
        reply->set_value(*warmReply->get_value());
      }
      return reply;
    }
    return coldReply;
  }

  ////////////////////////////////gets////////////////////////////////////
  McReply route(const McRequestWithMcOp<mc_op_gets>& req) {
    auto coldReply = cold_->route(req);
    if (coldReply.isHit()) {
      return coldReply;
    }

    // miss: send simple get to warm route
    McRequestWithMcOp<mc_op_get> reqGet(req.clone());
    auto warmReply = warm_->route(reqGet);
    uint32_t exptime;
    if (warmReply.isHit() && getExptimeForCold(req, exptime)) {
      // update cold route if we have the value
      auto addReq = coldUpdateFromWarm<McRequestWithMcOp<mc_op_add>>(
          req, warmReply, exptime);
      cold_->route(addReq);
      // and grab cas token again
      return cold_->route(req);
    }
    return coldReply;
  }

  TypedThriftReply<cpp2::McGetsReply> route(
      const TypedThriftRequest<cpp2::McGetsRequest>& req) {

    auto coldReply = cold_->route(req);
    if (coldReply.isHit()) {
      return coldReply;
    }

    // miss: send simple get to warm route
    TypedThriftRequest<cpp2::McGetRequest> reqGet(req.fullKey());
    auto warmReply = warm_->route(reqGet);
    uint32_t exptime;
    if (warmReply.isHit() && getExptimeForCold(req, exptime)) {
      // update cold route if we have the value
      auto addReq = coldUpdateFromWarm<TypedThriftRequest<cpp2::McAddRequest>>(
          req, warmReply, exptime);
      cold_->route(addReq);
      // and grab cas token again
      return cold_->route(req);
    }
    return coldReply;
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    // client is responsible for consistency of warm route, do not replicate
    // any update/delete operations
    return cold_->route(req);
  }

 private:
  const std::shared_ptr<RouteHandleIf> warm_;
  const std::shared_ptr<RouteHandleIf> cold_;
  const folly::Optional<uint32_t> exptime_;

  template <class ToRequest, class Request, class Reply>
  static ToRequest coldUpdateFromWarm(const Request& origReq,
                                      const Reply& reply,
                                      uint32_t exptime) {
    ToRequest req(origReq.fullKey());
    folly::IOBuf cloned;
    if (const auto* valuePtr = reply.valuePtrUnsafe()) {
      valuePtr->cloneInto(cloned);
    }
    req.setValue(std::move(cloned));
    req.setFlags(reply.flags());
    req.setExptime(exptime);
    return req;
  }

  template <class Request>
  bool getExptimeForCold(const Request& req, uint32_t& exptime) {
    if (exptime_.hasValue()) {
      exptime = *exptime_;
      return true;
    }
    TypedThriftRequest<cpp2::McMetagetRequest> reqMetaget(req.fullKey());
    auto warmMeta = warm_->route(reqMetaget);
    const auto warmExptime =
      warmMeta->get_exptime() ? *warmMeta->get_exptime() : 0;
    if (warmMeta.isHit()) {
      exptime = warmExptime;
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

}}}  // facebook::memcache::mcrouter
