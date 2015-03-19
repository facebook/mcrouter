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
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/Reply.h"

namespace facebook { namespace memcache {

/**
 * This route handle is intended to be used as the 'to' route in a Migrate
 * route. It allows for substantial changes in the number of boxes in a pool
 * without increasing the miss rate and, subsequently, the load on the
 * underlying storage or service.
 * All sets and deletes go to the target ("cold") route handle.
 * Gets are attempted on the "cold" route handle and in case of a miss, data is
 * fetched from the "warm" route handle (where the request is likely to result
 * in a cache hit). If "warm" returns an hit, the response is then forwarded to
 * the client and an asynchronous request, with the configured expiration time,
 * updates the value in the "cold" route handle.
 */
template <class RouteHandleIf, typename AddOperation>
class WarmUpRoute {
 public:
  static std::string routeName() { return "warm-up"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    return {cold_, warm_};
  }

  WarmUpRoute(std::shared_ptr<RouteHandleIf> warmh,
              std::shared_ptr<RouteHandleIf> coldh,
              uint32_t exptime,
              size_t ncacheExptime = 0,
              size_t ncacheUpdatePeriod = 0)
  : warm_(std::move(warmh)),
    cold_(std::move(coldh)),
    exptime_(exptime),
    ncacheExptime_(ncacheExptime),
    ncacheUpdatePeriod_(ncacheUpdatePeriod),
    ncacheUpdateCounter_(ncacheUpdatePeriod) {

    assert(warm_ != nullptr);
    assert(cold_ != nullptr);
  }

  WarmUpRoute(RouteHandleFactory<RouteHandleIf>& factory,
              const folly::dynamic& json,
              uint32_t exptime)
      : exptime_(exptime) {

    checkLogic(json.isObject(), "WarmUpRoute should be object");
    checkLogic(json.count("cold"), "WarmUpRoute: no cold route");
    checkLogic(json.count("warm"), "WarmUpRoute: no warm route");
    if (json.count("exptime")) {
      checkLogic(json["exptime"].isInt(),
                 "WarmUpRoute: exptime is not integer");
      exptime_ = json["exptime"].asInt();
    }

    cold_ = factory.create(json["cold"]);
    warm_ = factory.create(json["warm"]);

    assert(cold_ != nullptr);
    assert(warm_ != nullptr);
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, typename GetLike<Operation>::Type = 0) {

    using Reply = typename ReplyType<Operation, Request>::type;

    auto coldReply = cold_->route(req, Operation());
    if (coldReply.isHit()) {
      if (coldReply.flags() & MC_MSG_FLAG_NEGATIVE_CACHE) {
        if (ncacheUpdatePeriod_) {
          if (ncacheUpdateCounter_ == 1) {
            updateColdNcache(req, Operation());
            ncacheUpdateCounter_ = ncacheUpdatePeriod_;
          } else {
            --ncacheUpdateCounter_;
          }
        }

        /* return a miss */
        coldReply = Reply(DefaultReply, Operation());
      }
      return coldReply;
    }

    /* else */
    auto warmReply = warm_->route(req, Operation());
#ifdef __clang__
#pragma clang diagnostic push // ignore generalized lambda capture warning
#pragma clang diagnostic ignored "-Wc++1y-extensions"
#endif
    if (warmReply.isHit()) {
      fiber::addTask([cold = cold_,
                      addReq = coldUpdateFromWarm(req, warmReply, exptime_)]() {
        cold->route(addReq, AddOperation());
      });
    } else if (warmReply.isMiss() && ncacheUpdatePeriod_) {
      fiber::addTask([cold = cold_,
                      addReq = coldNcache(req, ncacheExptime_)]() {
        cold->route(addReq, AddOperation());
      });
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    return warmReply;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, OtherThanT(Operation, GetLike<>) = 0) const {

    return cold_->route(req, Operation());
  }

 private:
  std::shared_ptr<RouteHandleIf> warm_;
  std::shared_ptr<RouteHandleIf> cold_;
  uint32_t exptime_{0};
  size_t ncacheExptime_{0};
  size_t ncacheUpdatePeriod_{0};
  size_t ncacheUpdateCounter_{0};

  template <class Request, class Reply>
  static Request coldUpdateFromWarm(const Request& origReq,
                                    const Reply& reply,
                                    size_t exptime) {
    auto req = origReq.clone();
    folly::IOBuf cloned;
    reply.value().cloneInto(cloned);
    req.setValue(std::move(cloned));
    req.setFlags(reply.flags());
    req.setExptime(exptime);
    return std::move(req);
  }

  template <class Request>
  static Request coldNcache(const Request& origReq, size_t ncacheExptime) {
    auto req = origReq.clone();
    req.setValue(
      folly::IOBuf(
        folly::IOBuf::COPY_BUFFER, "ncache"));
    req.setFlags(MC_MSG_FLAG_NEGATIVE_CACHE);
    req.setExptime(ncacheExptime);
    return std::move(req);
  }

  template <class Request, class Operation>
  void updateColdNcache(const Request& req, Operation) {
#ifdef __clang__
#pragma clang diagnostic push // ignore generalized lambda capture warning
#pragma clang diagnostic ignored "-Wc++1y-extensions"
#endif
    fiber::addTask(
      [cold = cold_,
       warm = warm_,
       creq = Request(req.clone()),
       exptime = exptime_,
       ncacheExptime = ncacheExptime_]() {
        auto warmReply = warm->route(creq, Operation());
        if (warmReply.isHit()) {
          cold->route(coldUpdateFromWarm(creq, warmReply, exptime),
                      McOperation<mc_op_set>());
        } else {
          /* bump TTL on the ncache entry */
          cold->route(coldNcache(creq, ncacheExptime),
                      McOperation<mc_op_set>());
        }
      }
    );
  }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
};

}}
