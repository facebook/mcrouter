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

#include <folly/dynamic.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"

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
              uint32_t exptime)
  : warm_(std::move(warmh)),
    cold_(std::move(coldh)),
    exptime_(exptime) {

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
    const Request& req, Operation, typename GetLike<Operation>::Type = 0)
    const {

    auto coldReply = cold_->route(req, Operation());
    if (coldReply.isHit()) {
      return coldReply;
    }

    /* else */
    auto warmReply = warm_->route(req, Operation());
    if (warmReply.isHit()) {
      auto addReq = req.clone();
      folly::IOBuf cloned;
      warmReply.value().cloneInto(cloned);
      addReq.setValue(std::move(cloned));
      addReq.setFlags(warmReply.flags());
      addReq.setExptime(exptime_);

      auto wrappedAddReq = folly::MoveWrapper<Request>(std::move(addReq));

      auto cold = cold_;
      fiber::addTask([cold, wrappedAddReq]() {
        cold->route(std::move(*wrappedAddReq), AddOperation());
      });
    }
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
  uint32_t exptime_;
};

}}
