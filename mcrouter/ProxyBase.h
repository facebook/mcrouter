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

#include <cassert>
#include <cstddef>
#include <random>

#include <folly/fibers/FiberManager.h>

#include "mcrouter/AsyncLog.h"
#include "mcrouter/config.h"
#include "mcrouter/ProxyStats.h"

namespace folly {
class EventBase;
}

namespace facebook {
namespace memcache {

class McrouterOptions;

namespace mcrouter {

class McrouterInstanceBase;
class ProxyDestinationMap;

class ProxyBase {
 public:
  ProxyBase(McrouterInstanceBase& rtr, size_t id, folly::EventBase& evb);
  virtual ~ProxyBase() = default;

  const McrouterInstanceBase& router() const {
    return router_;
  }
  McrouterInstanceBase& router() {
    return router_;
  }

  size_t getId() const {
    return id_;
  }

  /**
   * This method is equal to router().opts(), with the only difference,
   * that it doesn't require the caller to know about McrouterInstanceBase.
   * This allows to break include cycles.
   */
  const McrouterOptions& getRouterOptions() const;

  folly::EventBase& eventBase() {
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

 private:
  McrouterInstanceBase& router_;
  const size_t id_{0};

  folly::EventBase& eventBase_;
  folly::fibers::FiberManager fiberManager_;

  AsyncLog asyncLog_;

  std::mt19937 randomGenerator_;

  ProxyStats stats_;
  std::unique_ptr<ProxyStatsContainer> statsContainer_;

 protected:
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
