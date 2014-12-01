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
#include <vector>

#include <folly/dynamic.h>
#include <folly/MoveWrapper.h>
#include <folly/Range.h>

#include "mcrouter/config.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/OperationTraits.h"
#include "mcrouter/routes/ShardSplitter.h"

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Splits given request according to shard splits provided by ShardSplitter
 */
template <class RouteHandleIf>
class ShardSplitRoute {
 public:
  static std::string routeName() { return "shard-split"; }

  ShardSplitRoute(std::shared_ptr<RouteHandleIf> rh,
                  ShardSplitter shardSplitter)
    : rh_(std::move(rh)),
      shardSplitter_(std::move(shardSplitter)) {
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    if (!GetLike<Operation>::value && !DeleteLike<Operation>::value) {
      return rh_->couldRouteTo(req, Operation());
    }

    folly::StringPiece shard;
    auto cnt = shardSplitter_.getShardSplitCnt(req.routingKey(), shard);
    if (cnt == 1) {
      return rh_->couldRouteTo(req, Operation());
    }
    if (GetLike<Operation>::value) {
      size_t i = globals::hostid() % cnt;
      if (i == 0) {
        return rh_->couldRouteTo(req, Operation());
      }
      return rh_->couldRouteTo(splitReq(req, i - 1, shard), Operation());
    }

    assert(DeleteLike<Operation>::value);
    auto result = rh_->couldRouteTo(req, Operation());
    for (size_t i = 0; i < cnt - 1; ++i) {
      auto nx = rh_->couldRouteTo(splitReq(req, i, shard), Operation());
      result.insert(result.end(), nx.begin(), nx.end());
    }

    return result;
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
    auto r = rh_;
    for (size_t i = 0; i < cnt - 1; ++i) {
      auto wrappedReq = folly::makeMoveWrapper(splitReq(req, i, shard));
      fiber::addTask(
        [r, wrappedReq]() {
          r->route(*wrappedReq, Operation());
        });
    }
    return rh_->route(req, Operation());
  }

 private:
  std::shared_ptr<RouteHandleIf> rh_;
  const ShardSplitter shardSplitter_;

  // from request with key 'prefix:shard:suffix' creates a copy of
  // request with key 'prefix:shardXY:suffix'
  template <class Request>
  Request splitReq(const Request& req, size_t offset,
                   folly::StringPiece shard) const {
    std::string newKey;
    newKey.reserve(req.fullKey().size() + 2);
    newKey.append(req.fullKey().begin(), shard.end());
    newKey.push_back('a' + (offset % 26));
    newKey.push_back('a' + (offset / 26));
    newKey.append(shard.end(), req.fullKey().end());
    auto reqCopy = req.clone();
    reqCopy.setKey(newKey);
    return reqCopy;
  }
};

}}}  // facebook::memcache::mcrouter
