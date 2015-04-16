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
#include <utility>

#include <folly/experimental/fibers/FiberManager.h>

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyRequestContext;

namespace fiber_local { namespace detail {

struct McrouterFiberContext {
  std::shared_ptr<ProxyRequestContext> sharedCtx;
};

struct SharedCtxGuard {
  SharedCtxGuard(const SharedCtxGuard&) = delete;
  SharedCtxGuard& operator=(const SharedCtxGuard&) = delete;

  explicit SharedCtxGuard(std::shared_ptr<ProxyRequestContext> ctx) {
    auto& sctx = folly::fibers::local<McrouterFiberContext>().sharedCtx;
    assert(!sctx);
    sctx = std::move(ctx);
  }

  SharedCtxGuard(SharedCtxGuard&& other) noexcept
  : responsible_(other.responsible_) {
    other.responsible_ = false;
  }

  ~SharedCtxGuard() {
    if (responsible_) {
      folly::fibers::local<McrouterFiberContext>().sharedCtx.reset();
    }
  }
 private:
  bool responsible_{true};
};

}  // detail

using ContextTypeTag = folly::fibers::LocalType<detail::McrouterFiberContext>;

/**
 * Update ProxyRequestContext for current fiber (thread, if we're not on fiber)
 *
 * @return guard that will clear the context on destruction
 */
inline detail::SharedCtxGuard
setSharedCtx(std::shared_ptr<ProxyRequestContext> ctx) {
  return detail::SharedCtxGuard(std::move(ctx));
}

/**
 * Get ProxyRequestContext of current fiber (thread, if we're not on fiber)
 */
inline const std::shared_ptr<ProxyRequestContext>& getSharedCtx() {
  return folly::fibers::local<detail::McrouterFiberContext>().sharedCtx;
}

}}}}  // facebook::memcache::mcrouter::fiber_local
