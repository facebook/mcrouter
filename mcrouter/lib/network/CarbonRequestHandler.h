/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#pragma once

#include <folly/io/async/Request.h>

#include "mcrouter/lib/carbon/RequestReplyUtil.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/network/CarbonMessageDispatcher.h"
#include "mcrouter/lib/network/CaretHeader.h"
#include "mcrouter/lib/network/FBTrace.h"

namespace carbon {

template <class OnRequest, class RequestList>
class CarbonRequestHandler : public facebook::memcache::CarbonMessageDispatcher<
                                 RequestList,
                                 CarbonRequestHandler<OnRequest, RequestList>,
                                 facebook::memcache::McServerRequestContext&&> {
 public:
  template <class... Args>
  explicit CarbonRequestHandler(Args&&... args)
      : onRequest_(std::forward<Args>(args)...) {}

  template <class Request>
  void onRequest(
      facebook::memcache::McServerRequestContext&& ctx,
      Request&& req) {
    onRequestImpl(
        std::move(ctx),
        std::move(req),
        nullptr /* headerInfo */,
        nullptr /* reqBuffer */,
        carbon::detail::CanHandleRequest::value<Request, OnRequest>());
  }

  template <class Request>
  void onRequest(
      facebook::memcache::McServerRequestContext&& ctx,
      Request&& req,
      const facebook::memcache::CaretMessageInfo& headerInfo,
      const folly::IOBuf& reqBuf) {
    onRequestImpl(
        std::move(ctx),
        std::move(req),
        &headerInfo,
        &reqBuf,
        carbon::detail::CanHandleRequest::value<Request, OnRequest>());
  }

 private:
  OnRequest onRequest_;

  template <class Request>
  void onRequestImpl(
      facebook::memcache::McServerRequestContext&& ctx,
      Request&& req,
      const facebook::memcache::CaretMessageInfo* headerInfo,
      const folly::IOBuf* reqBuf,
      std::true_type) {
    if (UNLIKELY(
            facebook::memcache::getFbTraceInfo(req) != nullptr &&
            facebook::memcache::traceCheckRateLimit())) {
      onRequestImplWithTracingEnabled(
          std::move(ctx), std::move(req), headerInfo, reqBuf);
      return;
    }
    callOnRequest(
        std::move(ctx),
        std::move(req),
        headerInfo,
        reqBuf,
        carbon::detail::CanHandleRequestWithBuffer::
            value<Request, OnRequest>());
  }

  template <class Request>
  void onRequestImpl(
      facebook::memcache::McServerRequestContext&&,
      Request&&,
      const facebook::memcache::CaretMessageInfo*,
      const folly::IOBuf*,
      std::false_type) {
    facebook::memcache::throwRuntime(
        "onRequest for {} not defined", typeid(Request).name());
  }

  template <class Request>
  FOLLY_NOINLINE void onRequestImplWithTracingEnabled(
      facebook::memcache::McServerRequestContext&& ctx,
      Request&& req,
      const facebook::memcache::CaretMessageInfo* headerInfo,
      const folly::IOBuf* reqBuf) {
    folly::RequestContextScopeGuard requestContextGuard;
    const auto commId =
        facebook::memcache::traceRequestReceived(std::cref(req).get());
    callOnRequest(
        std::move(ctx),
        std::move(req),
        headerInfo,
        reqBuf,
        carbon::detail::CanHandleRequestWithBuffer::
            value<Request, OnRequest>());
    facebook::memcache::traceRequestHandlerCompleted(commId);
  }

  template <class Request>
  void callOnRequest(
      facebook::memcache::McServerRequestContext&& ctx,
      Request&& req,
      const facebook::memcache::CaretMessageInfo* headerInfo,
      const folly::IOBuf* reqBuf,
      std::true_type) {
    onRequest_.onRequest(std::move(ctx), std::move(req), headerInfo, reqBuf);
  }

  template <class Request>
  void callOnRequest(
      facebook::memcache::McServerRequestContext&& ctx,
      Request&& req,
      const facebook::memcache::CaretMessageInfo*,
      const folly::IOBuf*,
      std::false_type) {
    onRequest_.onRequest(std::move(ctx), std::move(req));
  }
};

} // namespace carbon
