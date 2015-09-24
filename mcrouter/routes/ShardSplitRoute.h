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

#include <folly/Range.h>
#include <folly/dynamic.h>
#include <folly/experimental/fibers/FiberManager.h>

#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/ShardSplitter.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Create a suffix for the shard ID which will make the key route to
 * the n'th shard split as specified in 'offset'.
 *
 * @param offset Which split the new key should route to.
 * @return A suffix suitable to be appended to a shard ID in a key.
 */
std::string shardSplitSuffix(size_t offset);

/**
 * Create a key which matches 'fullKey' except has a suffix on the shard
 * portion which will make the key route to the n'th shard split as specified in
 * 'index'.
 *
 * @param fullKey The key to re-route to a split shard.
 * @param offset Which split the new key should route to.
 * @param shard The shard portion of fullKey. Must be a substring of 'fullKey'
 *              and can be obtained via getShardId().
 * @return A new key which routes to the n'th shard split for 'shard'.
 */
std::string createSplitKey(folly::StringPiece fullKey,
                           size_t offset,
                           folly::StringPiece shard);

/**
 * Splits given request according to shard splits provided by ShardSplitter
 */
class ShardSplitRoute {
 public:
  static std::string routeName() { return "shard-split"; }

  ShardSplitRoute(McrouterRouteHandlePtr rh, ShardSplitter shardSplitter)
    : rh_(std::move(rh)),
      shardSplitter_(std::move(shardSplitter)) {
  }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<McrouterRouteHandleIf>& t) const {
    auto& ctx = fiber_local::getSharedCtx();
    if (ctx) {
      ctx->recordShardSplitter(shardSplitter_);
    }

    if (!GetLike<Operation>::value && !DeleteLike<Operation>::value) {
      t(*rh_, req, Operation());
      return;
    }

    folly::StringPiece shard;
    auto cnt = shardSplitter_.getShardSplitCnt(req.routingKey(), shard);
    if (cnt == 1) {
      t(*rh_, req, Operation());
      return;
    }
    if (GetLike<Operation>::value) {
      size_t i = globals::hostid() % cnt;
      if (i == 0) {
        t(*rh_, req, Operation());
        return;
      }
      t(*rh_, splitReq(req, i - 1, shard), Operation());
      return;
    }

    assert(DeleteLike<Operation>::value);
    t(*rh_, req, Operation());
    for (size_t i = 0; i < cnt - 1; ++i) {
      t(*rh_, splitReq(req, i, shard), Operation());
    }
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    typename GetLike<Operation>::Type = 0) const {

    // Gets are routed to one of the splits.
    folly::StringPiece shard;
    auto cnt = shardSplitter_.getShardSplitCnt(req.routingKey(), shard);
    size_t i = globals::hostid() % cnt;
    if (i == 0) {
      return rh_->route(req, Operation());
    }
    return rh_->route(splitReq(req, i - 1, shard), Operation());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation,
    OtherThanT(Operation, GetLike<>) = 0) const {

    // Anything that is not a Get or Delete goes to the primary split.
    static_assert(!GetLike<Operation>::value, "");
    if (!DeleteLike<Operation>::value) {
      return rh_->route(req, Operation());
    }

    // Deletes are broadcast to all splits.
    folly::StringPiece shard;
    auto cnt = shardSplitter_.getShardSplitCnt(req.routingKey(), shard);
    for (size_t i = 0; i < cnt - 1; ++i) {
      folly::fibers::addTask(
        [r = rh_, req_ = splitReq(req, i, shard)]() {
          r->route(req_, Operation());
        });
    }
    return rh_->route(req, Operation());
  }

 private:
  McrouterRouteHandlePtr rh_;
  const ShardSplitter shardSplitter_;

  // from request with key 'prefix:shard:suffix' creates a copy of
  // request with key 'prefix:shardXY:suffix'
  template <class Request>
  Request splitReq(const Request& req, size_t offset,
                   folly::StringPiece shard) const {
    auto reqCopy = req.clone();
    reqCopy.setKey(createSplitKey(req.fullKey(), offset, shard));
    return reqCopy;
  }
};

}}}  // facebook::memcache::mcrouter
