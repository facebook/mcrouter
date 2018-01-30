/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <random>
#include <string>
#include <utility>

#include <folly/Conv.h>
#include <folly/Random.h>
#include <folly/Range.h>
#include <folly/io/IOBuf.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/network/gen/MemcacheMessages.h"
#include "mcrouter/lib/network/gen/MemcacheRouteHandleIf.h"

namespace folly {
struct dynamic;
} // namespace folly

namespace facebook {
namespace memcache {

template <class RouteHandleIf>
class RouteHandleFactory;

namespace mcrouter {

namespace detail {
constexpr size_t numDigitsBase10(uint64_t n) {
  return n < 10 ? 1 : 1 + numDigitsBase10(n / 10);
}
} // namespace detail

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
class L1L2SizeSplitRoute {
 public:
  static std::string routeName() {
    return "l1l2-sizesplit";
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<MemcacheRouteHandleIf>& t) const {
    t(*l1_, req);
    t(*l2_, req);
  }

  L1L2SizeSplitRoute(
      std::shared_ptr<MemcacheRouteHandleIf> l1,
      std::shared_ptr<MemcacheRouteHandleIf> l2,
      size_t threshold,
      int32_t ttlThreshold,
      int32_t failureTtl,
      bool bothFullSet)
      : l1_(std::move(l1)),
        l2_(std::move(l2)),
        threshold_(threshold),
        ttlThreshold_(ttlThreshold),
        failureTtl_(failureTtl),
        bothFullSet_(bothFullSet) {
    assert(l1_ != nullptr);
    assert(l2_ != nullptr);
    folly::Random::seed(randomGenerator_);
  }

  McGetReply route(const McGetRequest& req) const;
  McSetReply route(const McSetRequest& req) const;

  McLeaseGetReply route(const McLeaseGetRequest& req) const;
  McLeaseSetReply route(const McLeaseSetRequest& req) const;

  // All other operations should route to L1
  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    return l1_->route(req);
  }

 private:
  static constexpr folly::StringPiece kHashAlias = "|==|";
  static constexpr size_t kMaxMcKeyLength = 255;
  static constexpr size_t kExtraKeySpaceNeeded = kHashAlias.size() +
      detail::numDigitsBase10(std::numeric_limits<uint64_t>::max());

  const std::shared_ptr<MemcacheRouteHandleIf> l1_;
  const std::shared_ptr<MemcacheRouteHandleIf> l2_;
  const size_t threshold_{0};
  const int32_t ttlThreshold_{0};
  const int32_t failureTtl_{0};
  const bool bothFullSet_{false};
  mutable std::mt19937 randomGenerator_;

  McLeaseGetReply doLeaseGetRoute(
      const McLeaseGetRequest& req,
      size_t retriesLeft) const;

  template <class Suffix>
  static std::string makeL2Key(folly::StringPiece key, Suffix randomSuffix) {
    return folly::to<std::string>(key, kHashAlias, randomSuffix);
  }

  template <class Request>
  bool fullSetShouldGoToL1(const Request& req) const {
    return threshold_ == 0 ||
        req.value().computeChainDataLength() < threshold_ ||
        (ttlThreshold_ != 0 && req.exptime() < ttlThreshold_) ||
        req.key().fullKey().size() + kExtraKeySpaceNeeded > kMaxMcKeyLength;
  }
};

std::shared_ptr<MemcacheRouteHandleIf> makeL1L2SizeSplitRoute(
    RouteHandleFactory<MemcacheRouteHandleIf>& factory,
    const folly::dynamic& json);

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
