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
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
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

  template <class Request>
  void traverse(const Request& req,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*l1_, req);
    t(*l2_, req);
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

  template <class Request>
  ReplyT<Request> route(const Request& req, GetLikeT<Request> = 0) {
    using AddRequest = AddT<Request>;

    auto l1Reply = l1_->route(req);
    if (l1Reply.isHit()) {
      if (l1Reply.flags() & MC_MSG_FLAG_NEGATIVE_CACHE) {
        if (ncacheUpdatePeriod_) {
          if (ncacheUpdateCounter_ == 1) {
            updateL1Ncache(req);
            ncacheUpdateCounter_ = ncacheUpdatePeriod_;
          } else {
            --ncacheUpdateCounter_;
          }
        }

        /* return a miss */
        l1Reply = ReplyT<Request>(DefaultReply, req);
      }
      return l1Reply;
    }

    /* else */
    auto l2Reply = l2_->route(req);
    if (l2Reply.isHit()) {
      folly::fibers::addTask(
        [l1 = l1_,
         addReq = l1UpdateFromL2<AddRequest>(
           req, l2Reply, upgradingL1Exptime_)]() {

          l1->route(addReq);
        });
    } else if (l2Reply.isMiss() && ncacheUpdatePeriod_) {
      folly::fibers::addTask(
        [l1 = l1_, addReq = l1Ncache<AddRequest>(req, ncacheExptime_)]() {
          l1->route(addReq);
        });
    }
    return l2Reply;
  }

  template <class Request>
  ReplyT<Request> route(const Request& req,
                        OtherThanT<Request, GetLike<>> = 0) const {

    return l1_->route(req);
  }

 private:
  const std::shared_ptr<RouteHandleIf> l1_;
  const std::shared_ptr<RouteHandleIf> l2_;
  const uint32_t upgradingL1Exptime_{0};
  size_t ncacheExptime_{0};
  size_t ncacheUpdatePeriod_{0};
  size_t ncacheUpdateCounter_{0};

  template <class Request>
  struct AddImpl {};

  template <int op>
  struct AddImpl<McRequestWithMcOp<op>> {
    using type = McRequestWithMcOp<mc_op_add>;
  };

  template <class M>
  struct AddImpl<TypedThriftRequest<M>> {
    using type = TypedThriftRequest<cpp2::McAddRequest>;
  };

  template <class Request>
  struct SetImpl {};

  template <int op>
  struct SetImpl<McRequestWithMcOp<op>> {
    using type = McRequestWithMcOp<mc_op_set>;
  };

  template <class M>
  struct SetImpl<TypedThriftRequest<M>> {
    using type = TypedThriftRequest<cpp2::McSetRequest>;
  };

  template <class Request>
  using AddT = typename AddImpl<Request>::type;

  template <class Request>
  using SetT = typename SetImpl<Request>::type;

  template <class ToRequest, class Request, class Reply>
  static ToRequest l1UpdateFromL2(const Request& origReq,
                                  const Reply& reply,
                                  size_t upgradingL1Exptime) {
    ToRequest req;
    req.setKey(origReq.key());
    folly::IOBuf cloned;
    if (reply.valuePtrUnsafe() != nullptr) {
      reply.valuePtrUnsafe()->cloneInto(cloned);
    }
    req.setValue(std::move(cloned));
    req.setFlags(reply.flags());
    req.setExptime(upgradingL1Exptime);
    return req;
  }

  template <class ToRequest, class Request>
  static ToRequest l1Ncache(const Request& origReq, size_t ncacheExptime) {
    ToRequest req;
    req.setKey(origReq.key());
    req.setValue(folly::IOBuf(folly::IOBuf::COPY_BUFFER, "ncache"));
    req.setFlags(MC_MSG_FLAG_NEGATIVE_CACHE);
    req.setExptime(ncacheExptime);
    return std::move(req);
  }

  template <class Request>
  void updateL1Ncache(const Request& req) {
    using SetRequest = SetT<Request>;

    folly::fibers::addTask(
      [l1 = l1_, l2 = l2_, creq = req.clone(),
       upgradingL1Exptime = upgradingL1Exptime_,
       ncacheExptime = ncacheExptime_]() {
        auto l2Reply = l2->route(creq);
        if (l2Reply.isHit()) {
          l1->route(l1UpdateFromL2<SetRequest>(
                creq, l2Reply, upgradingL1Exptime));
        } else {
          /* bump TTL on the ncache entry */
          l1->route(l1Ncache<SetRequest>(creq, ncacheExptime));
        }
      }
    );
  }
};

}} // facebook::memcache
