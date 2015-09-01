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

#include <folly/experimental/fibers/FiberManager.h>

#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/IOBufUtil.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/network/AccessPoint.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/ShadowRouteIf.h"
#include "mcrouter/ShadowValidationData.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <class ShadowPolicy>
template <class Operation, class Request, class Reply>
void ShadowRoute<ShadowPolicy>::sendAndValidateRequest(
    const Reply& normalReply,
    std::shared_ptr<McrouterRouteHandleIf> shadow,
    std::shared_ptr<Request> adjustedReq,
    Operation) const {

  dispatchShadowRequest(std::move(shadow), std::move(adjustedReq), Operation());
};

template <class ShadowPolicy>
template <class Request, class Reply>
void ShadowRoute<ShadowPolicy>::sendAndValidateRequest(
    const Reply& normalReply,
    std::shared_ptr<McrouterRouteHandleIf> shadow,
    std::shared_ptr<Request> adjustedReq,
    McOperation<mc_op_get>) const {

  uint64_t flags = normalReply.flags();
  mc_res_t result = normalReply.result();
  size_t hashVal = folly::IOBufHash()(normalReply.value());

  auto normalDest = normalReply.destination();

  folly::fibers::addTask(
    [shadow = std::move(shadow),
     adjustedReq = std::move(adjustedReq),
     flags,
     result,
     hashVal,
     normalDest = std::move(normalDest)]() mutable {
      // we don't want to spool shadow request
      fiber_local::clearAsynclogName();
      fiber_local::addRequestClass(RequestClass::kShadow);

      auto shadowReply = shadow->route(*adjustedReq,
                                       McOperation<mc_op_get>());
      uint64_t shadowFlags = shadowReply.flags();
      mc_res_t shadowResult = shadowReply.result();
      size_t shadowHash = folly::IOBufHash()(shadowReply.value());

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
                                            adjustedReq->fullKey()};

        logShadowValidationError(proxy, validationData);
      }
    });
};

template <class ShadowPolicy>
template <class Operation, class Request>
void ShadowRoute<ShadowPolicy>::dispatchShadowRequest(
    std::shared_ptr<McrouterRouteHandleIf> shadow,
    std::shared_ptr<Request> adjustedReq,
    Operation) const {

  folly::fibers::addTask(
    [shadow = std::move(shadow), adjustedReq = std::move(adjustedReq)]() {
      // we don't want to spool shadow requests
      fiber_local::clearAsynclogName();
      fiber_local::addRequestClass(RequestClass::kShadow);
      shadow->route(*adjustedReq, Operation());
    });
};

}}} // facebook::memcache::mcrouter
