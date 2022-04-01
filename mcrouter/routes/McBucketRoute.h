/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/ProxyRequestContextTyped.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/network/gen/MemcacheRouteHandleIf.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"

namespace facebook::memcache::mcrouter {

struct McBucketRouteSettings {
  size_t totalBuckets;
  size_t bucketizeUntil;
};

/**
 * The route handle is intended to be used to enable bucket-based routing.
 * When enabled, the router will consistently hash the key to a specific
 * bucket out of configured "totalBuckets" count, construct the routing key
 * based on the resulted bucket and route the request based on this key.
 *
 * Config:
 * - bucketize(bool) - enable the bucketization
 * - total_buckets(int) - total number of buckets
 * - bucketize_until(int) - enable the handle for buckets until (exclusive)
 *   this number. Must be less than total_buckets. Needed for gradual migration.
 */
class McBucketRoute {
 public:
  std::string routeName() const {
    return folly::sformat(
        "bucketize|total_buckets={},bucketize_until={}",
        totalBuckets_,
        bucketizeUntil_);
  }

  template <class Request>
  bool traverse(
      const Request& req,
      const RouteHandleTraverser<MemcacheRouteHandleIf>& t) const {
    // todo
    return t(*rh_, req);
  }

  McBucketRoute(
      std::shared_ptr<MemcacheRouteHandleIf> rh,
      McBucketRouteSettings& settings)
      : rh_(std::move(rh)),
        totalBuckets_(settings.totalBuckets),
        bucketizeUntil_(settings.bucketizeUntil) {}

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    auto proxy = &fiber_local<MemcacheRouterInfo>::getSharedCtx()->proxy();
    proxy->stats().increment(bucketized_routing_stat);
    // todo:
    // 1. hash key to bucketId
    // 2. check if bucketId < bucketizeUntil
    // 3. if no: rh_->route(req)
    // 4. if yes: clone req with bucketIdString
    // 5. rh_->route(reqClone)
    return rh_->route(req);
  }

 private:
  const std::shared_ptr<MemcacheRouteHandleIf> rh_;
  const size_t totalBuckets_{0};
  const size_t bucketizeUntil_{0};
};

std::shared_ptr<MemcacheRouteHandleIf> makeMcBucketRoute(
    std::shared_ptr<MemcacheRouteHandleIf> rh,
    const folly::dynamic& json);

} // namespace facebook::memcache::mcrouter
