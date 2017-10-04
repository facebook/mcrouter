/*
 *  Copyright (c) 2017, Facebook, Inc.
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

#include <folly/fibers/FiberManager.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/carbon/RoutingGroups.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/gen/Memcache.h"

namespace facebook {
namespace memcache {

/**
 * This route handle is intended to be used for asymmetric data storage.
 *
 * Values smaller than threshold are only written to L1 pool. Above, and
 * a small sentinel value is left in the L1 pool. Optionally, full data
 * can be written to both pools.
 *
 * Currently, only plain sets and gets are supported.
 *
 * There are potential consistency issues in both routes, no lease support, etc.
 */
template <class RouteHandleIf>
class L1L2SizeSplitRoute {
 public:
  static std::string routeName() {
    return "l1l2-sizesplit";
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*l1_, req);
    t(*l2_, req);
  }

  L1L2SizeSplitRoute(
      std::shared_ptr<RouteHandleIf> l1,
      std::shared_ptr<RouteHandleIf> l2,
      size_t threshold,
      bool bothFullSet)
      : l1_(std::move(l1)),
        l2_(std::move(l2)),
        threshold_(threshold),
        bothFullSet_(bothFullSet) {
    assert(l1_ != nullptr);
    assert(l2_ != nullptr);
  }

  McGetReply route(const McGetRequest& req) {
    auto l1Reply = l1_->route(req);
    if (isHitResult(l1Reply.result()) &&
        (l1Reply.flags() & MC_MSG_FLAG_SIZE_SPLIT)) {
      /* Real value lives in the L2 pool. */
      return l2_->route(req);
    }

    /* If it was a miss or a non-special value, pass along. */
    return l1Reply;
  }

  McSetReply route(const McSetRequest& req) {
    if (threshold_ == 0 || req.value().computeChainDataLength() < threshold_) {
      return l1_->route(req);
    }

    /* Value is large enough to warrant split sets */
    if (bothFullSet_) {
      /* Send the full size value to both pools. Server should age out values,
       * turning them into sentinels
       */
      auto l1Reply = l1_->route(req);
      if (isStoredResult(l1Reply.result())) {
        /* This is technically racy, but we're fine for these experiments. */
        folly::fibers::addTask([l2 = l2_, creq = req]() { l2->route(creq); });
      }

      /* Errors on L2 only mean we'll end up recaching early.
       * Need to add counters to monitor for severe issues with L2
       */
      return l1Reply;
    } else {
      /* Store to L2 first to avoid race. */
      auto l2Reply = l2_->route(req);

      if (isStoredResult(l2Reply.result())) {
        auto l1Sentinel = req;
        l1Sentinel.flags() |= MC_MSG_FLAG_SIZE_SPLIT;
        l1Sentinel.value() = folly::IOBuf();
        auto l1Reply = l1_->route(l1Sentinel);
        return l1Reply;
      }

      return l2Reply;
    }
  }

  /* add/gets/cas/incr/decr/delete should all route to L1 for now */
  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    return l1_->route(req);
  }

 private:
  const std::shared_ptr<RouteHandleIf> l1_;
  const std::shared_ptr<RouteHandleIf> l2_;
  const size_t threshold_{0};
  const bool bothFullSet_{false};
};
} // namespace memcache
} // namespace facebook
