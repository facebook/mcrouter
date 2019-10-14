/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <array>
#include <chrono>
#include <memory>

#include <folly/IntrusiveList.h>

#include "mcrouter/ExponentialSmoothData.h"
#include "mcrouter/TkoLog.h"
#include "mcrouter/lib/carbon/Result.h"
#include "mcrouter/lib/network/Transport.h"

namespace folly {
class AsyncTimeout;
} // namespace folly

namespace facebook {
namespace memcache {

struct AccessPoint;
struct RpcStatsContext;

namespace mcrouter {

class ProxyBase;
class TkoTracker;

class ProxyDestinationBase {
 public:
  using RequestQueueStats = Transport::RequestQueueStats;

  enum class State {
    New, // never connected
    Up, // currently connected
    Down, // currently down
    Closed, // closed due to inactive
    NumStates
  };

  struct Stats {
    State state{State::New};
    ExponentialSmoothData<16> avgLatency;
    std::unique_ptr<
        std::array<uint64_t, static_cast<size_t>(carbon::Result::NUM_RESULTS)>>
        results;
    size_t probesSent{0};
    double retransPerKByte{0.0};
    // If poolstats config is present, keep track of most recent
    // pool with this destination
    int32_t poolStatIndex_{-1};

    // last time this connection was closed due to inactivity
    uint64_t inactiveConnectionClosedTimestampUs{0};
  };

  ProxyDestinationBase(
      ProxyBase& proxy,
      std::shared_ptr<const AccessPoint> ap,
      std::chrono::milliseconds timeout,
      uint64_t qosClass,
      uint64_t qosPath,
      folly::StringPiece routerInfoName);
  virtual ~ProxyDestinationBase();

  /**
   * Returns true if okay to send request using this client.
   *
   * @param tkoReason   Output argument that will have the TKO reason in case
   *                    this method returns true.
   *
   * @return  True iff it is okay to send a request using this client.
   *          False otherwise.
   */
  bool maySend(carbon::Result& tkoReason) const;

  /**
   * @return stats for ProxyDestination
   */
  const Stats& stats() const {
    return stats_;
  }

  /**
   * Destination host that this client talks to.
   */
  const std::shared_ptr<const AccessPoint>& accessPoint() const {
    return accessPoint_;
  }

  /**
   * The proxy that owns this client.
   */
  ProxyBase& proxy() const {
    return proxy_;
  }

  void updateShortestTimeout(
      std::chrono::milliseconds connectTimeout,
      std::chrono::milliseconds writeTimeout);

  /**
   * If the connection was previously closed due to lack of activity,
   * log for how long it was closed.
   */
  void updateConnectionClosedInternalStat();

  std::shared_ptr<TkoTracker> tracker() const {
    return tracker_;
  }
  void setTracker(std::shared_ptr<TkoTracker> tracker) {
    tracker_ = std::move(tracker);
  }

  void setPoolStatsIndex(int32_t index);
  void updatePoolStatConnections(bool connected);

  /**
   * Sets the key for this proxy destination. This proxy destination will only
   * store the StringPiece, so the string has to be kept alive by the caller.
   */
  void setKey(folly::StringPiece key) {
    key_ = key;
  }
  folly::StringPiece key() const {
    return key_;
  }

  virtual RequestQueueStats getRequestStats() const = 0;

  /**
   * Closes transport connection.
   */
  virtual void resetInactive() = 0;

 protected:
  virtual void updateTransportTimeoutsIfShorter(
      std::chrono::milliseconds shortestConnectTimeout,
      std::chrono::milliseconds shortestWriteTimeout) = 0;
  virtual carbon::Result sendProbe() = 0;
  virtual std::weak_ptr<ProxyDestinationBase> selfPtr() = 0;

  void markAsActive();
  void setState(State st);

  void handleTko(const carbon::Result result, bool isProbeRequest);
  void onTransitionToState(State state);
  void onTransitionFromState(State state);

  Stats& stats() {
    return stats_;
  }
  std::chrono::milliseconds shortestConnectTimeout() const {
    return shortestConnectTimeout_;
  }
  std::chrono::milliseconds shortestWriteTimeout() const {
    return shortestWriteTimeout_;
  }
  uint64_t qosClass() const {
    return qosClass_;
  }
  uint64_t qosPath() const {
    return qosPath_;
  }
  folly::StringPiece routerInfoName() const {
    return routerInfoName_;
  }
  bool probeInflight() const {
    return probeInflight_;
  }

 private:
  ProxyBase& proxy_;
  std::shared_ptr<TkoTracker> tracker_;

  // Destination host information
  const std::shared_ptr<const AccessPoint> accessPoint_;
  std::chrono::milliseconds shortestConnectTimeout_{0};
  std::chrono::milliseconds shortestWriteTimeout_{0};
  const uint64_t qosClass_{0};
  const uint64_t qosPath_{0};
  const folly::StringPiece routerInfoName_;

  Stats stats_;

  // Fields related to probes (for un-TKO).
  std::unique_ptr<folly::AsyncTimeout> probeTimer_;
  int probeDelayNextMs{0};
  bool probeInflight_{false};

  // The string is stored in ProxyDestinationMap::destinations_
  folly::StringPiece key_; ///< consists of AccessPoint, and timeout

  void* stateList_{nullptr};
  folly::IntrusiveListHook stateListHook_;

  void onTkoEvent(TkoLogEvent event, carbon::Result result) const;

  void startSendingProbes();
  void stopSendingProbes();
  void scheduleNextProbe();

  void onTransitionImpl(State state, bool to);

  friend class ProxyDestinationMap;
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
