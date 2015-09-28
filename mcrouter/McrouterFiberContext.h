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
#include <folly/Range.h>
#include <folly/ScopeGuard.h>

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyClientCommon;
class ProxyRequestContext;

class RequestClass {
 public:
  static const RequestClass kFailover;
  static const RequestClass kShadow;

  constexpr RequestClass() {}

  void add(RequestClass rc) {
    mask_ |= rc.mask_;
  }

  bool is(RequestClass rc) const {
    return (mask_ & rc.mask_) == rc.mask_;
  }

  bool isNormal() const {
    return mask_ == 0;
  }

  const char* toString() const;
 private:
  explicit constexpr RequestClass(uint32_t value)
    : mask_(value) {
  }

  uint32_t mask_{0};
};

namespace fiber_local { namespace detail {

struct McrouterFiberContext {
  std::shared_ptr<ProxyRequestContext> sharedCtx;

  folly::StringPiece asynclogName;

  RequestClass requestClass;

  bool failoverTag{false};

  bool failoverDisabled{false};
};

}  // detail

using ContextTypeTag = folly::fibers::LocalType<detail::McrouterFiberContext>;

/**
 * Clear all locals, run `f`, restore locals
 */
template <typename F>
inline typename std::result_of<F()>::type runWithoutLocals(F&& f) {
  auto tmp = std::move(folly::fibers::local<detail::McrouterFiberContext>());
  folly::fibers::local<detail::McrouterFiberContext>() =
    detail::McrouterFiberContext();
  auto guard = folly::makeGuard([&tmp]() mutable {
    folly::fibers::local<detail::McrouterFiberContext>() = std::move(tmp);
  });

  return f();
}

/**
 * Copy all locals, run `f`, restore locals
 */
template <typename F>
inline typename std::result_of<F()>::type runWithLocals(F&& f) {
  auto tmp = folly::fibers::local<detail::McrouterFiberContext>();
  auto guard = folly::makeGuard([&tmp]() mutable {
    folly::fibers::local<detail::McrouterFiberContext>() = std::move(tmp);
  });

  return f();
}

/**
 * Update ProxyRequestContext for current fiber (thread, if we're not on fiber)
 */
inline void setSharedCtx(std::shared_ptr<ProxyRequestContext> ctx) {
  folly::fibers::local<detail::McrouterFiberContext>().sharedCtx =
    std::move(ctx);
}

/**
 * Get ProxyRequestContext of current fiber (thread, if we're not on fiber)
 */
inline const std::shared_ptr<ProxyRequestContext>& getSharedCtx() {
  return folly::fibers::local<detail::McrouterFiberContext>().sharedCtx;
}

/**
 * Add a RequestClass for current fiber (thread, if we're not on fiber)
 */
inline void addRequestClass(RequestClass value) {
  folly::fibers::local<detail::McrouterFiberContext>().requestClass.add(value);
}

/**
 * Get RequestClass of current fiber (thread, if we're not on fiber)
 */
inline RequestClass getRequestClass() {
  return folly::fibers::local<detail::McrouterFiberContext>().requestClass;
}

/**
 * Update AsynclogName for current fiber (thread, if we're not on fiber)
 */
inline void setAsynclogName(folly::StringPiece value) {
  folly::fibers::local<detail::McrouterFiberContext>().asynclogName = value;
}

/**
 * Clear AsynclogName for current fiber (thread, if we're not on fiber)
 */
inline void clearAsynclogName() {
  setAsynclogName("");
}

/**
 * Get asynclog name of current fiber (thread, if we're not on fiber)
 */
inline folly::StringPiece getAsynclogName() {
  return folly::fibers::local<detail::McrouterFiberContext>().asynclogName;
}

/**
 * Update failover tag for current fiber (thread, if we're not on fiber)
 */
inline void setFailoverTag(bool value) {
  folly::fibers::local<detail::McrouterFiberContext>().failoverTag = value;
}

/**
 * Get failover tag of current fiber (thread, if we're not on fiber)
 */
inline bool getFailoverTag() {
  return folly::fibers::local<detail::McrouterFiberContext>().failoverTag;
}

/**
 * Set failover disabled flag for current fiber (thread, if we're not on fiber)
 */
inline void setFailoverDisabled(bool value) {
  folly::fibers::local<detail::McrouterFiberContext>().failoverDisabled = value;
}

/**
 * Get failover disabled tag of current fiber (thread, if we're not on fiber)
 */
inline bool getFailoverDisabled() {
  return folly::fibers::local<detail::McrouterFiberContext>().failoverDisabled;
}

}}}}  // facebook::memcache::mcrouter::fiber_local
