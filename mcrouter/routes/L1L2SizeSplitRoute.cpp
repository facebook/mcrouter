/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "L1L2SizeSplitRoute.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <folly/Range.h>
#include <folly/dynamic.h>
#include <folly/fibers/FiberManager.h>

#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/mc/msg.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

McGetReply L1L2SizeSplitRoute::route(const McGetRequest& req) const {
  // Set flag on the read path. Server will only return back sentinel values
  // when this flag is present in order to accommodate old clients.
  // TODO It's probably fine to const_cast and avoid the copy here
  const auto l1ReqWithFlag = [&req]() {
    auto r = req;
    r.flags() |= MC_MSG_FLAG_SIZE_SPLIT;
    return r;
  }();
  auto reply = l1_->route(l1ReqWithFlag);
  if (isHitResult(reply.result()) && (reply.flags() & MC_MSG_FLAG_SIZE_SPLIT)) {
    // Real value lives in the L2 pool
    // Note that for both get and set, keys read from/written to L2 are not
    // suffixed with |==|<rand>.
    reply = l2_->route(req);
  }
  // If we didn't get a sentinel value, pass along
  return reply;
}

McSetReply L1L2SizeSplitRoute::route(const McSetRequest& req) const {
  if (fullSetShouldGoToL1(req)) {
    return l1_->route(req);
  }

  // Value is large enough to warrant split sets
  if (bothFullSet_) {
    auto l1Reply = l1_->route(req);
    if (isStoredResult(l1Reply.result())) {
      folly::fibers::addTask([l2 = l2_, req]() { l2->route(req); });
    }
    return l1Reply;
  } else {
    // Store to L2 first to avoid race
    auto reply = l2_->route(req);
    if (isStoredResult(reply.result())) {
      // Set key to L1 with empty value and special sentinel flag
      const auto l1Sentinel = [&req]() {
        auto r = req;
        r.value() = folly::IOBuf();
        r.flags() |= MC_MSG_FLAG_SIZE_SPLIT;
        return r;
      }();
      reply = l1_->route(l1Sentinel);
    }
    return reply;
  }
}

McLeaseGetReply L1L2SizeSplitRoute::route(const McLeaseGetRequest& req) const {
  return doLeaseGetRoute(req, 1 /* retriesLeft */);
}

McLeaseGetReply L1L2SizeSplitRoute::doLeaseGetRoute(
    const McLeaseGetRequest& req,
    size_t retriesLeft) const {
  constexpr uint64_t kHotMissToken = 1;

  // Set flag on the read path. Server will only return back sentinel values
  // when this flag is present in order to accommodate old clients.
  // TODO It's probably fine to const_cast and avoid the copy here
  const auto l1ReqWithFlag = [&req]() {
    auto r = req;
    r.flags() |= MC_MSG_FLAG_SIZE_SPLIT;
    return r;
  }();
  auto l1Reply = l1_->route(l1ReqWithFlag);

  // If we didn't receive a sentinel value, return immediately.
  if (!(l1Reply.flags() & MC_MSG_FLAG_SIZE_SPLIT)) {
    return l1Reply;
  }

  // We got an L1 sentinel, but a nonzero lease token. Note that we may convert
  // a stale hit on a sentinel to a regular lease miss or hot miss.
  if (static_cast<uint64_t>(l1Reply.leaseToken()) >= kHotMissToken) {
    McLeaseGetReply reply(mc_res_notfound);
    reply.leaseToken() = l1Reply.leaseToken();
    return reply;
  }

  // At this point, we got a non-stale sentinel hit from L1.
  const auto l1Value = coalesceAndGetRange(l1Reply.value());
  const auto l2Req = l1Value.empty()
      ? McGetRequest(req.key().fullKey())
      : McGetRequest(makeL2Key(req.key().fullKey(), l1Value));

  auto l2Reply = l2_->route(l2Req);
  if (isHitResult(l2Reply.result())) {
    McLeaseGetReply reply(mc_res_found);
    reply.flags() = l2Reply.flags();
    reply.value() = std::move(l2Reply.value());
    return reply;
  }

  if (isErrorResult(l2Reply.result())) {
    return McLeaseGetReply(l2Reply.result());
  } else if (retriesLeft != 0) {
    [&]() {
      McGetsRequest l1GetsRequest(req.key().fullKey());
      auto l1GetsReply = l1_->route(l1GetsRequest);
      if (isHitResult(l1GetsReply.result()) &&
          coalesceAndGetRange(l1GetsReply.value()) == l1Value) {
        McCasRequest l1CasRequest(req.key().fullKey());
        l1CasRequest.casToken() = l1GetsReply.casToken();
        l1CasRequest.exptime() = -1;
        l1_->route(l1CasRequest);
      }
    }();
    return doLeaseGetRoute(req, retriesLeft - 1);
  } else {
    return McLeaseGetReply(mc_res_local_error);
  }
}

McLeaseSetReply L1L2SizeSplitRoute::route(const McLeaseSetRequest& req) const {
  if (fullSetShouldGoToL1(req)) {
    return l1_->route(req);
  }

  if (bothFullSet_) {
    auto adjustedReq = [&]() {
      auto r = req;
      r.key() = makeL2Key(
          req.key().fullKey(), folly::to<std::string>(randomGenerator_()));
      return r;
    }();
    auto l1Reply = l1_->route(adjustedReq);

    if (isStoredResult(l1Reply.result())) {
      auto makeL2Req = [&]() {
        McSetRequest r(adjustedReq.key().fullKey());
        r.flags() = req.flags();
        r.exptime() = req.exptime();
        r.value() = std::move(adjustedReq.value());
        return r;
      };
      folly::fibers::addTask(
          [l2 = l2_, l2Req = makeL2Req()] { l2->route(l2Req); });
    }
    return l1Reply;
  } else {
    // Generate a random integer that will be used as the value for the L1 item
    // and will be mixed into the key for L2.
    const auto randInt = randomGenerator_();
    auto l2SetReq = [&]() {
      McSetRequest r(makeL2Key(req.key().fullKey(), randInt));
      r.value() = req.value();
      r.flags() = req.flags();
      r.exptime() = req.exptime();
      return r;
    }();
    auto l2Reply = l2_->route(l2SetReq);

    if (isStoredResult(l2Reply.result())) {
      const auto l1SentinelReq = [&]() {
        auto r = req;
        r.flags() |= MC_MSG_FLAG_SIZE_SPLIT;
        r.value() = folly::IOBuf(
            folly::IOBuf::CopyBufferOp(), folly::to<std::string>(randInt));
        return r;
      }();

      return l1_->route(l1SentinelReq);
    } else {
      // If L2 is failing, cut the exptime down and store full value to L1.
      const auto l1FallbackReq = [&]() {
        auto r = req;
        if (r.exptime() > failureTtl_ || r.exptime() == 0) {
          r.exptime() = failureTtl_;
        }
        return r;
      }();
      return l1_->route(l1FallbackReq);
    }
  }
}

std::shared_ptr<MemcacheRouteHandleIf> makeL1L2SizeSplitRoute(
    RouteHandleFactory<MemcacheRouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "L1L2SizeSplitRoute should be an object");
  checkLogic(json.count("l1"), "L1L2SizeSplitRoute: no l1 route");
  checkLogic(json.count("l2"), "L1L2SizeSplitRoute: no l2 route");
  checkLogic(json.count("threshold"), "L1L2SizeSplitRoute: no threshold");
  checkLogic(
      json["threshold"].isInt(),
      "L1L2SizeSplitRoute: threshold is not an integer");
  size_t threshold = json["threshold"].getInt();

  int32_t ttlThreshold = 0;
  if (json.count("ttl_threshold")) {
    checkLogic(
        json["ttl_threshold"].isInt(),
        "L1L2SizeSplitRoute: ttl_threshold is not an integer");
    ttlThreshold = json["ttl_threshold"].getInt();
    checkLogic(
        ttlThreshold >= 0,
        "L1L2SizeSplitRoute: ttl_threshold must be nonnegative");
  }

  int32_t failureTtl = 60;
  if (json.count("failure_ttl")) {
    checkLogic(
        json["failure_ttl"].isInt(),
        "L1L2SizeSplitRoute: failure_ttl is not an integer");
    failureTtl = json["failure_ttl"].getInt();
    checkLogic(
        failureTtl >= 0, "L1L2SizeSplitRoute: failure_ttl must be nonnegative");
    checkLogic(
        failureTtl != 0, "L1L2SizeSplitRoute: failure_ttl must not be zero");
  }

  bool bothFullSet = false;
  if (json.count("both_full_set")) {
    checkLogic(
        json["both_full_set"].isBool(),
        "L1L2SizeSplitRoute: both_full_set is not a boolean");
    bothFullSet = json["both_full_set"].getBool();
  }

  return std::make_shared<MemcacheRouteHandle<L1L2SizeSplitRoute>>(
      factory.create(json["l1"]),
      factory.create(json["l2"]),
      threshold,
      ttlThreshold,
      failureTtl,
      bothFullSet);
}

constexpr folly::StringPiece L1L2SizeSplitRoute::kHashAlias;

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
