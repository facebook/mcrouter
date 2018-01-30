/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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
#include <folly/fibers/FiberManager.h>

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/carbon/RoutingGroups.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/ShardSplitter.h"

namespace facebook {
namespace memcache {

template <class RouterInfo>
class RouteHandleFactory;

namespace mcrouter {

/**
 * Create a suffix for the shard ID which will make the key route to
 * the shard split as specified in 'offset'.
 *
 * @param offset Which split the new key should route to,
 *   must be in the range [0, nsplits).
 *   The primary split is at offset 0 and does not have a suffix.
 * @return A suffix suitable to be appended to a shard ID in a key.
 *   Empty string if offset is 0.
 */
std::string shardSplitSuffix(size_t offset);

namespace detail {
/**
 * Create a key which matches 'fullKey' except has a suffix on the shard
 * portion which will make the key route to the shard split as specified in
 * 'offset'.
 *
 * @param fullKey The key to re-route to a split shard.
 * @param offset Which split the new key should route to,
 *   must be in the range [0, nsplits).
 *   The primary split is at offset 0 and does not have a suffix.
 * @param shard The shard portion of fullKey. Must be a substring of 'fullKey'
 *              and can be obtained via getShardId().
 * @return A new key which routes to the proper shard split for 'shard'.
 */
std::string createSplitKey(
    folly::StringPiece fullKey,
    size_t offset,
    folly::StringPiece shard);
} // detail

/**
 * Splits given request according to shard splits provided by ShardSplitter
 */
template <class RouterInfo>
class ShardSplitRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;

 public:
  static std::string routeName() {
    return "shard-split";
  }

  ShardSplitRoute(
      std::shared_ptr<RouteHandleIf> rh,
      ShardSplitter shardSplitter)
      : rh_(std::move(rh)), shardSplitter_(std::move(shardSplitter)) {}

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    auto* ctx = fiber_local<RouterInfo>::getTraverseCtx();
    if (ctx) {
      ctx->recordShardSplitter(shardSplitter_);
    }

    folly::StringPiece shard;
    auto split = shardSplitter_.getShardSplit(req.key().routingKey(), shard);

    if (split == nullptr) {
      t(*rh_, req);
      return;
    }

    auto splitSize = split->getSplitSizeForCurrentHost();
    if (carbon::DeleteLike<Request>::value && split->fanoutDeletesEnabled()) {
      // Note: the order here is part of the API and must not be changed.
      // We traverse the primary split and then other splits in order.
      t(*rh_, req);
      for (size_t i = 1; i < splitSize; ++i) {
        t(*rh_, splitReq(req, i, shard));
      }
    } else {
      size_t i = globals::hostid() % splitSize;
      // Note that foreachPossibleClient always calls traverse on a request with
      // no flags set.
      if (i == 0) {
        t(*rh_, req);
        return;
      }
      t(*rh_, splitReq(req, i, shard));
    }
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    folly::StringPiece shard;
    auto split = shardSplitter_.getShardSplit(req.key().routingKey(), shard);
    if (split == nullptr) {
      return rh_->route(req);
    }

    size_t splitSize = split->getSplitSizeForCurrentHost();
    if (splitSize == 1) {
      return rh_->route(req);
    }

    if (carbon::DeleteLike<Request>::value && split->fanoutDeletesEnabled()) {
      for (size_t i = 1; i < splitSize; ++i) {
        folly::fibers::addTask(
            [ r = rh_, req_ = splitReq(req, i, shard) ]() { r->route(req_); });
      }
      return rh_->route(req);
    } else {
      size_t i = globals::hostid() % splitSize;
      if (i == 0) {
        return rh_->route(req);
      }
      return rh_->route(splitReq(req, i, shard));
    }
  }

 private:
  std::shared_ptr<RouteHandleIf> rh_;
  const ShardSplitter shardSplitter_;

  // from request with key 'prefix:shard:suffix' creates a copy of
  // request with key 'prefix:shardXY:suffix'
  template <class Request>
  Request splitReq(const Request& req, size_t offset, folly::StringPiece shard)
      const {
    auto reqCopy = req;
    reqCopy.key() = detail::createSplitKey(req.key().fullKey(), offset, shard);
    return reqCopy;
  }
};

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeShardSplitRoute(
    std::shared_ptr<typename RouterInfo::RouteHandleIf> rh,
    ShardSplitter shardSplitter) {
  return makeRouteHandleWithInfo<RouterInfo, ShardSplitRoute>(
      std::move(rh), std::move(shardSplitter));
}

} // mcrouter
} // memcache
} // facebook
