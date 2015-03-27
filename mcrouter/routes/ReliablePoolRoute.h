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

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/routes/AllSyncRoute.h"
#include "mcrouter/lib/routes/ErrorRoute.h"
#include "mcrouter/lib/routes/FailoverRoute.h"
#include "mcrouter/lib/routes/HashRoute.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/proxy.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * For delete type requests it will send the request to all the
 * salts and return the most awful reply back.
 *
 * For update and get operation types, it will route the request to
 * first salt and if error reply received it will try the next in
 * the list and continue so on until an non-error reply is receieved.
 * If all the requests fails it will return the reply from the last
 * salt.
 *
 * For all other operation reply from the first salted route is returned.
 */
template <class RouteHandleIf, class HashFunc>
class ReliablePoolRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;

  static std::string routeName() { return "reliable-pool"; }

  ReliablePoolRoute(std::vector<std::shared_ptr<RouteHandleIf>> destinations,
                    HashFunc hashFunc,
                    std::string init_salt,
                    size_t failoverCount)
      : destinations_(std::move(destinations)),
        failoverCount_(failoverCount),
        rhs_(makeFailoverTargets(std::move(init_salt))),
        failoverRoute_(rhs_),
        allSyncRoute_(rhs_) {
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return destinations_;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx,
    typename GetLike<Operation>::Type = 0) const {

    return failoverRoute_.route(req, Operation(), ctx);
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx,
    typename UpdateLike<Operation>::Type = 0) const {

    return failoverRoute_.route(req, Operation(), ctx);
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx,
    typename DeleteLike<Operation>::Type = 0) const {

    return allSyncRoute_.route(req, Operation(), ctx);
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx,
    OtherThanT(Operation, GetLike<>, UpdateLike<>, DeleteLike<>) = 0) const {

    if (rhs_.empty()) {
      return NullRoute<RouteHandleIf>::route(req, Operation(), ctx);
    }
    return rhs_.front()->route(req, Operation(), ctx);
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> destinations_;
  size_t failoverCount_;
  const std::vector<std::shared_ptr<HashRoute<RouteHandleIf, HashFunc>>> rhs_;
  const FailoverRoute<HashRoute<RouteHandleIf, HashFunc>> failoverRoute_;
  const AllSyncRoute<HashRoute<RouteHandleIf, HashFunc>> allSyncRoute_;

  std::vector<std::shared_ptr<HashRoute<RouteHandleIf, HashFunc>>>
  makeFailoverTargets(std::string init_salt) const {
    std::vector<std::shared_ptr<HashRoute<RouteHandleIf, HashFunc>>> ret;
    ret.push_back(std::make_shared<
      HashRoute<RouteHandleIf, HashFunc>>(
        destinations_,
        std::move(init_salt),
        HashFunc(destinations_.size())));
    for (size_t i = 0; i < failoverCount_; i++) {
      auto salt = folly::to<std::string>("salt", i);
      ret.push_back(std::make_shared<
        HashRoute<RouteHandleIf, HashFunc>>(
          destinations_,
          salt,
          HashFunc(destinations_.size())));
    }
    return ret;
  }
};

}}}  // facebook::memcache::mcrouter
