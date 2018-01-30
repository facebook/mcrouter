/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cassert>
#include <cstddef>
#include <random>

#include <folly/dynamic.h>
#include <folly/fibers/FiberManager.h>
#include <folly/io/async/VirtualEventBase.h>

#include "mcrouter/AsyncLog.h"
#include "mcrouter/ProxyStats.h"
#include "mcrouter/config.h"

namespace facebook {
namespace memcache {

class McrouterOptions;

namespace mcrouter {

class CarbonRouterInstanceBase;
class ProxyDestinationMap;

class ProxyBase {
 public:
  using FlushList = boost::intrusive::list<
      folly::EventBase::LoopCallback,
      boost::intrusive::constant_time_size<false>>;

  template <class RouterInfo>
  ProxyBase(
      CarbonRouterInstanceBase& rtr,
      size_t id,
      folly::VirtualEventBase& evb,
      RouterInfo tag);

  virtual ~ProxyBase() = default;

  const CarbonRouterInstanceBase& router() const {
    return router_;
  }
  CarbonRouterInstanceBase& router() {
    return router_;
  }

  size_t getId() const {
    return id_;
  }

  /**
   * This method is equal to router().opts(), with the only difference,
   * that it doesn't require the caller to know about CarbonRouterInstanceBase.
   * This allows to break include cycles.
   */
  const McrouterOptions& getRouterOptions() const;

  folly::VirtualEventBase& eventBase() {
    return eventBase_;
  }

  folly::fibers::FiberManager& fiberManager() {
    return fiberManager_;
  }

  ProxyDestinationMap* destinationMap() {
    return destinationMap_.get();
  }

  AsyncLog& asyncLog() {
    return asyncLog_;
  }

  std::mt19937& randomGenerator() {
    return randomGenerator_;
  }

  ProxyStats& stats() {
    return stats_;
  }
  const ProxyStats& stats() const {
    return stats_;
  }

  ProxyStatsContainer* statsContainer() {
    return statsContainer_.get();
  }

  /** Will let through requests from the above queue if we have capacity */
  virtual void pump() = 0;

  /**
   * @return Current value of the relaxed notification period if set.
   */
  virtual size_t queueNotifyPeriod() const = 0;

  virtual folly::dynamic dumpRequestStats(bool filterZeroes) const = 0;

  /** Advance the request stats bin. */
  virtual void advanceRequestStatsBin() = 0;

  FlushList& flushList() {
    return flushList_;
  }

 private:
  CarbonRouterInstanceBase& router_;
  const size_t id_{0};

  folly::VirtualEventBase& eventBase_;
  folly::fibers::FiberManager fiberManager_;

  AsyncLog asyncLog_;

  std::mt19937 randomGenerator_;

  ProxyStats stats_;
  std::unique_ptr<ProxyStatsContainer> statsContainer_;

  static folly::fibers::FiberManager::Options getFiberManagerOptions(
      const McrouterOptions& opts);

 protected:
  // A queue of callbacks for flushing requests in AsyncMcClients.
  FlushList flushList_;

  class FlushCallback : public folly::EventBase::LoopCallback {
   public:
    explicit FlushCallback(ProxyBase& proxy) : proxy_(proxy) {}
    void setList(FlushList flushList) {
      flushList_ = std::move(flushList);
    }
    void runLoopCallback() noexcept override final;

   private:
    ProxyBase& proxy_;
    FlushList flushList_;
    bool rescheduled_{false};
  } flushCallback_;

  std::unique_ptr<ProxyDestinationMap> destinationMap_;

  /**
   * Incoming request rate limiting.
   *
   * We need this to protect memory and CPU intensive routing code from
   * processing too many requests at a time. The limit here ensures that
   * in an event we get a spike of incoming requests, we'll queue up
   * proxy_request_t objects, which don't consume nearly as much memory as
   * fiber stacks.
   */

  /** Number of requests processing */
  size_t numRequestsProcessing_{0};
  /** Number of waiting requests */
  size_t numRequestsWaiting_{0};

  friend class ProxyRequestContext;
};
} // mcrouter
} // memcache
} // facebook

#include "ProxyBase-inl.h"
