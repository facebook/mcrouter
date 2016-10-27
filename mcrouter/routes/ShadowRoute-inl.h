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

#include <folly/fibers/FiberManager.h>

#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/ShadowValidationData.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/carbon/RequestReplyUtil.h"
#include "mcrouter/lib/network/AccessPoint.h"
#include "mcrouter/routes/ShadowRouteIf.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <class RouteHandleIf, class ShadowPolicy>
template <class Request>
void ShadowRoute<RouteHandleIf, ShadowPolicy>::sendAndValidateRequest(
    const ReplyT<Request>& normalReply,
    std::shared_ptr<RouteHandleIf> shadow,
    std::shared_ptr<Request> adjustedReq) const {

  dispatchShadowRequest(std::move(shadow), std::move(adjustedReq));
}

template <class RouteHandleIf, class ShadowPolicy>
template <class GetRequest>
void ShadowRoute<RouteHandleIf, ShadowPolicy>::sendAndValidateRequestGetImpl(
    const ReplyT<GetRequest>& normalReply,
    std::shared_ptr<RouteHandleIf> shadow,
    std::shared_ptr<GetRequest> adjustedReq) const {
  uint64_t flags = carbon::getFlags(normalReply);

  mc_res_t result = normalReply.result();
  size_t hashVal = normalReply.value().hasValue()
      ? folly::IOBufHash()(*normalReply.value())
      : 0;

  auto normalDest = normalReply.destination();

  folly::fibers::addTask([
    shadow = std::move(shadow),
    adjustedReq = std::move(adjustedReq),
    flags,
    result,
    hashVal,
    normalDest = std::move(normalDest)
  ]() mutable {
    // we don't want to spool shadow request
    fiber_local::clearAsynclogName();
    fiber_local::addRequestClass(RequestClass::kShadow);

    auto shadowReply = shadow->route(*adjustedReq);
    uint64_t shadowFlags = carbon::getFlags(shadowReply);
    mc_res_t shadowResult = shadowReply.result();
    size_t shadowHash = shadowReply.value().hasValue()
        ? folly::IOBufHash()(*shadowReply.value())
        : 0;

    if (shadowFlags != flags || shadowResult != result ||
        hashVal != shadowHash) {
      auto& proxy = fiber_local::getSharedCtx()->proxy();

      ShadowValidationData validationData{McOperation<mc_op_get>().name,
                                          normalDest.get(),
                                          shadowReply.destination().get(),
                                          flags,
                                          shadowFlags,
                                          result,
                                          shadowResult,
                                          adjustedReq->key().fullKey()};

      logShadowValidationError(proxy, validationData);
    }
  });
}

template <class RouteHandleIf, class ShadowPolicy>
template <class Request>
void ShadowRoute<RouteHandleIf, ShadowPolicy>::dispatchShadowRequest(
    std::shared_ptr<RouteHandleIf> shadow,
    std::shared_ptr<Request> adjustedReq) const {
  folly::fibers::addTask(
      [ shadow = std::move(shadow), adjustedReq = std::move(adjustedReq) ]() {
        // we don't want to spool shadow requests
        fiber_local::clearAsynclogName();
        fiber_local::addRequestClass(RequestClass::kShadow);
        shadow->route(*adjustedReq);
      });
}

}}} // facebook::memcache::mcrouter
