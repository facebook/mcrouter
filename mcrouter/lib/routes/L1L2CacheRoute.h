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

#include <folly/experimental/fibers/FiberManager.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"

namespace facebook { namespace memcache {

/**
 * This route handle is intended to be used for two level caching.
 * For 'get' tries to find value in L1 cache, in case of a miss fetches value
 * from L2 cache and asynchronous 'add' request updates value in L1.
 *
 * Supports negative caching. If ncacheUpdatePeriod > 0, every miss from L2
 * will be stored in L1 as a special "ncache" value with "NEGATIVE_CACHE" flag
 * and ncacheExptime expiration time.
 * If we try to fetch "ncache" value from L1 we'll return a miss and refill
 * L1 from L2 every ncacheUpdatePeriod "ncache" requests.
 *
 * NOTE: Doesn't work with lease get, gets and metaget.
 * Always overrides expiration time for L2 -> L1 update request.
 * Client is responsible for L2 consistency, sets and deletes are forwarded
 * only to L1 cache.
 */
template <class RouteHandleIf>
class L1L2CacheRoute {
 public:
  static std::string routeName() { return "l1l2-cache"; }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*l1_, req, Operation());
    t(*l2_, req, Operation());
  }

  L1L2CacheRoute(std::shared_ptr<RouteHandleIf> l1,
                 std::shared_ptr<RouteHandleIf> l2,
                 uint32_t upgradingL1Exptime,
                 size_t ncacheExptime,
                 size_t ncacheUpdatePeriod)
  : l1_(std::move(l1)),
    l2_(std::move(l2)),
    upgradingL1Exptime_(upgradingL1Exptime),
    ncacheExptime_(ncacheExptime),
    ncacheUpdatePeriod_(ncacheUpdatePeriod),
    ncacheUpdateCounter_(ncacheUpdatePeriod) {

    assert(l1_ != nullptr);
    assert(l2_ != nullptr);
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, typename GetLike<Operation>::Type = 0) {

    using Reply = typename ReplyType<Operation, Request>::type;

    auto l1Reply = l1_->route(req, Operation());
    if (l1Reply.isHit()) {
      if (l1Reply.flags() & MC_MSG_FLAG_NEGATIVE_CACHE) {
        if (ncacheUpdatePeriod_) {
          if (ncacheUpdateCounter_ == 1) {
            updateL1Ncache(req, Operation());
            ncacheUpdateCounter_ = ncacheUpdatePeriod_;
          } else {
            --ncacheUpdateCounter_;
          }
        }

        /* return a miss */
        l1Reply = Reply(DefaultReply, Operation());
      }
      return l1Reply;
    }

    /* else */
    auto l2Reply = l2_->route(req, Operation());
    if (l2Reply.isHit()) {
      folly::fibers::addTask(
        [l1 = l1_,
         addReq = l1UpdateFromL2(req, l2Reply, upgradingL1Exptime_)]() {
          l1->route(addReq, McOperation<mc_op_add>());
        });
    } else if (l2Reply.isMiss() && ncacheUpdatePeriod_) {
      folly::fibers::addTask([ l1 = l1_,
                               addReq = l1Ncache(req, ncacheExptime_) ]() {
        l1->route(addReq, McOperation<mc_op_add>());
      });
    }
    return l2Reply;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, OtherThanT(Operation, GetLike<>) = 0) const {

    return l1_->route(req, Operation());
  }

 private:
  const std::shared_ptr<RouteHandleIf> l1_;
  const std::shared_ptr<RouteHandleIf> l2_;
  const uint32_t upgradingL1Exptime_{0};
  size_t ncacheExptime_{0};
  size_t ncacheUpdatePeriod_{0};
  size_t ncacheUpdateCounter_{0};

  template <class Request, class Reply>
  static Request l1UpdateFromL2(const Request& origReq,
                                const Reply& reply,
                                size_t upgradingL1Exptime) {
    auto req = origReq.clone();
    folly::IOBuf cloned;
    reply.value().cloneInto(cloned);
    req.setValue(std::move(cloned));
    req.setFlags(reply.flags());
    req.setExptime(upgradingL1Exptime);
    return std::move(req);
  }

  template <class Request>
  static Request l1Ncache(const Request& origReq, size_t ncacheExptime) {
    auto req = origReq.clone();
    req.setValue(folly::IOBuf(folly::IOBuf::COPY_BUFFER, "ncache"));
    req.setFlags(MC_MSG_FLAG_NEGATIVE_CACHE);
    req.setExptime(ncacheExptime);
    return std::move(req);
  }

  template <class Request, class Operation>
  void updateL1Ncache(const Request& req, Operation) {
    folly::fibers::addTask(
      [l1 = l1_, l2 = l2_, creq = Request(req.clone()),
       upgradingL1Exptime = upgradingL1Exptime_,
       ncacheExptime = ncacheExptime_]() {
        auto l2Reply = l2->route(creq, Operation());
        if (l2Reply.isHit()) {
          l1->route(l1UpdateFromL2(creq, l2Reply, upgradingL1Exptime),
                    McOperation<mc_op_set>());
        } else {
          /* bump TTL on the ncache entry */
          l1->route(l1Ncache(creq, ncacheExptime), McOperation<mc_op_set>());
        }
      }
    );
  }
};

}}
