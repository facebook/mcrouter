/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <bitset>
#include <memory>
#include <utility>

#include <folly/Range.h>
#include <folly/ScopeGuard.h>
#include <folly/fibers/FiberManager.h>

#include "mcrouter/lib/network/ServerLoad.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class RouterInfo>
class ProxyRequestContextWithInfo;

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
  explicit constexpr RequestClass(uint32_t value) : mask_(value) {}

  uint32_t mask_{0};
};

template <class RouterInfo>
class fiber_local {
 private:
  enum FeatureFlag : size_t {
    FAILOVER_TAG,
    FAILOVER_DISABLED,
    THRIFT_SERVER_LOAD_ENABLED,
    NUM_FLAGS
  };

  struct McrouterFiberContext {
    std::shared_ptr<ProxyRequestContextWithInfo<RouterInfo>> sharedCtx;
    folly::StringPiece asynclogName;
    ServerLoad load{0};
    RequestClass requestClass;
    int32_t selectedIndex{-1};
    int64_t networkTransportTimeUs{0};
    std::bitset<NUM_FLAGS> featureFlags;
  };

 public:
  using ContextTypeTag = folly::fibers::LocalType<McrouterFiberContext>;

  /**
   * Clear all locals, run `f`, restore locals
   */
  template <class F>
  static typename std::result_of<F()>::type runWithoutLocals(F&& f) {
    auto tmp = std::move(folly::fibers::local<McrouterFiberContext>());
    folly::fibers::local<McrouterFiberContext>() = McrouterFiberContext();
    auto guard = folly::makeGuard([&tmp]() mutable {
      folly::fibers::local<McrouterFiberContext>() = std::move(tmp);
    });

    return f();
  }

  /**
   * Copy all locals, run `f`, restore locals
   */
  template <class F>
  static typename std::result_of<F()>::type runWithLocals(F&& f) {
    auto tmp = folly::fibers::local<McrouterFiberContext>();
    auto guard = folly::makeGuard([&tmp]() mutable {
      folly::fibers::local<McrouterFiberContext>() = std::move(tmp);
    });

    return f();
  }

  /**
   * Update ProxyRequestContextWithInfo for current fiber (thread, if we're not
   * on fiber)
   */
  static void setSharedCtx(
      std::shared_ptr<ProxyRequestContextWithInfo<RouterInfo>> ctx) {
    folly::fibers::local<McrouterFiberContext>().sharedCtx = std::move(ctx);
  }

  /**
   * Get ProxyRequestContextWithInfo of current fiber (thread, if we're not on
   * fiber)
   */
  static const std::shared_ptr<ProxyRequestContextWithInfo<RouterInfo>>&
  getSharedCtx() {
    return folly::fibers::local<McrouterFiberContext>().sharedCtx;
  }

  /**
   * Get ProxyRequestContextWithInfo of current fiber (thread, if we're not on
   * fiber).
   * Can only be called from the RouteHandle's traverse() function.  Since
   * traverse() is not guaranteed to be called from the proxy thread, only
   * methods that access proxy/mcrouter in threadsafe way are allowed
   * to be called on the context.
   */
  static const ProxyRequestContextWithInfo<RouterInfo>* getTraverseCtx() {
    return folly::fibers::local<McrouterFiberContext>().sharedCtx.get();
  }

  /**
   * Add a RequestClass for current fiber (thread, if we're not on fiber)
   */
  static void addRequestClass(RequestClass value) {
    folly::fibers::local<McrouterFiberContext>().requestClass.add(value);
  }

  /**
   * Get RequestClass of current fiber (thread, if we're not on fiber)
   */
  static RequestClass getRequestClass() {
    return folly::fibers::local<McrouterFiberContext>().requestClass;
  }

  /**
   * Update AsynclogName for current fiber (thread, if we're not on fiber)
   */
  static void setAsynclogName(folly::StringPiece value) {
    folly::fibers::local<McrouterFiberContext>().asynclogName = value;
  }

  /**
   * Clear AsynclogName for current fiber (thread, if we're not on fiber)
   */
  static void clearAsynclogName() {
    setAsynclogName("");
  }

  /**
   * Get asynclog name of current fiber (thread, if we're not on fiber)
   */
  static folly::StringPiece getAsynclogName() {
    return folly::fibers::local<McrouterFiberContext>().asynclogName;
  }

  /**
   * Update failover tag for current fiber (thread, if we're not on fiber)
   */
  static void setFailoverTag(bool value) {
    folly::fibers::local<McrouterFiberContext>().featureFlags.set(
        FeatureFlag::FAILOVER_TAG, value);
  }

  /**
   * Get failover tag of current fiber (thread, if we're not on fiber)
   */
  static bool getFailoverTag() {
    return folly::fibers::local<McrouterFiberContext>().featureFlags.test(
        FeatureFlag::FAILOVER_TAG);
  }

  /**
   * Set selected index for normal reply from the target_ list.
   * it will be used for the iterator to avoid getting duplicates
   */
  static void setSelectedIndex(int32_t value) {
    folly::fibers::local<McrouterFiberContext>().selectedIndex = value;
  }

  /**
   * Get selected index for normal reply
   */
  static int32_t getSelectedIndex() {
    return folly::fibers::local<McrouterFiberContext>().selectedIndex;
  }

  /**
   * Set failover disabled flag for current fiber (thread, if we're not on
   * fiber)
   */
  static void setFailoverDisabled(bool value) {
    folly::fibers::local<McrouterFiberContext>().featureFlags.set(
        FeatureFlag::FAILOVER_DISABLED, value);
  }

  /**
   * Get failover disabled tag of current fiber (thread, if we're not on fiber)
   */
  static bool getFailoverDisabled() {
    return folly::fibers::local<McrouterFiberContext>().featureFlags.test(
        FeatureFlag::FAILOVER_DISABLED);
  }

  static void setServerLoad(ServerLoad load) {
    folly::fibers::local<McrouterFiberContext>().load = load;
  }

  static ServerLoad getServerLoad() {
    return folly::fibers::local<McrouterFiberContext>().load;
  }

  static void incNetworkTransportTimeBy(int64_t duration_us) {
    folly::fibers::local<McrouterFiberContext>().networkTransportTimeUs +=
        duration_us;
  }

  static int64_t getNetworkTransportTimeUs() {
    return folly::fibers::local<McrouterFiberContext>().networkTransportTimeUs;
  }

  static void setThriftServerLoadEnabled(bool value) {
    folly::fibers::local<McrouterFiberContext>().featureFlags.set(
        FeatureFlag::THRIFT_SERVER_LOAD_ENABLED, value);
  }

  static bool getThriftServerLoadEnabled() {
    return folly::fibers::local<McrouterFiberContext>().featureFlags.test(
        FeatureFlag::THRIFT_SERVER_LOAD_ENABLED);
  }
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
