/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <array>
#include <memory>
#include <string>

#include <folly/IntrusiveList.h>
#include <folly/Range.h>
#include <folly/SpinLock.h>

#include "mcrouter/ExponentialSmoothData.h"
#include "mcrouter/TkoLog.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/mc/msg.h"

namespace folly {
class AsyncTimeout;
} // namespace folly

namespace facebook {
namespace memcache {

struct AccessPoint;
class AsyncMcClient;
struct ReplyStatsContext;

namespace mcrouter {

class ProxyBase;
class ProxyDestinationMap;
class TkoTracker;

struct DestinationRequestCtx {
  int64_t startTime{0};
  int64_t endTime{0};

  explicit DestinationRequestCtx(int64_t now) : startTime(now) {}
};

class ProxyDestination {
 public:
  enum class State {
    kNew, // never connected
    kUp, // currently connected
    kDown, // currently down
    kClosed, // closed due to inactive
    kNumStates
  };

  struct Stats {
    State state{State::kNew};
    ExponentialSmoothData<16> avgLatency;
    std::unique_ptr<std::array<uint64_t, mc_nres>> results;
    size_t probesSent{0};
    double retransPerKByte{0.0};

    // last time this connection was closed due to inactivity
    uint64_t inactiveConnectionClosedTimestampUs{0};
  };

  ProxyBase& proxy; // for convenience

  std::shared_ptr<TkoTracker> tracker;

  ~ProxyDestination();

  /**
   * Sends a request to this destination.
   * NOTE: This is a blocking call that will return reply, once it's ready.
   *
   * @param request             The request to send.
   * @param requestContext      Context about this request.
   * @param timeout             The timeout of this call.
   * @param passThroughKey      Integer key that will be sent as an additional
   *                            field. If 0, it will not be sent.
   * @param replyStatsContext   Output argument with stats about the reply.
   */
  template <class Request>
  ReplyT<Request> send(
      const Request& request,
      DestinationRequestCtx& requestContext,
      std::chrono::milliseconds timeout,
      size_t passThroughKey,
      ReplyStatsContext& replyStatsContext);

  // returns true if okay to send req using this client
  bool maySend(mc_res_t& tkoReason) const;

  // Returns true if the current request should be dropped
  template <class Request>
  bool shouldDrop() const;

  /**
   * @return stats for ProxyDestination
   */
  const Stats& stats() const {
    return stats_;
  }

  const std::shared_ptr<const AccessPoint>& accessPoint() const {
    return accessPoint_;
  }

  void resetInactive();

  size_t getPendingRequestCount() const;
  size_t getInflightRequestCount() const;

  void updateShortestTimeout(std::chrono::milliseconds timeout);

  /**
   * Gracefully closes the connection, allowing it to properly drain if
   * possible.
   */
  void closeGracefully();

 private:
  std::unique_ptr<AsyncMcClient> client_;
  const std::shared_ptr<const AccessPoint> accessPoint_;
  // Ensure proxy thread doesn't reset AsyncMcClient
  // while config and stats threads may be accessing it
  mutable folly::SpinLock clientLock_;

  // Shortest timeout among all DestinationRoutes using this destination
  std::chrono::milliseconds shortestTimeout_{0};
  const uint64_t qosClass_{0};
  const uint64_t qosPath_{0};
  const folly::StringPiece routerInfoName_;

  Stats stats_;

  uint64_t lastRetransCycles_{0}; // Cycles when restransmits were last fetched
  uint64_t rxmitsToCloseConnection_{0};
  uint64_t lastConnCloseCycles_{0}; // Cycles when connection was last closed

  int probe_delay_next_ms{0};
  bool probeInflight_{false};
  // The string is stored in ProxyDestinationMap::destinations_
  folly::StringPiece pdstnKey_; ///< consists of ap, server_timeout

  std::unique_ptr<folly::AsyncTimeout> probeTimer_;

  static std::shared_ptr<ProxyDestination> create(
      ProxyBase& proxy,
      std::shared_ptr<AccessPoint> ap,
      std::chrono::milliseconds timeout,
      uint64_t qosClass,
      uint64_t qosPath,
      folly::StringPiece routerInfoName);

  void setState(State st);

  /**
   * If the connection was previously closed due to lack of activity,
   * log for how long it was closed.
   */
  void updateConnectionClosedInternalStat();

  void startSendingProbes();
  void stopSendingProbes();

  void scheduleNextProbe();

  void handleTko(const mc_res_t result, bool is_probe_req);

  // Process tko, stats and duration timer.
  void onReply(
      const mc_res_t result,
      DestinationRequestCtx& destreqCtx,
      const ReplyStatsContext& replyStatsContext,
      bool isRequestBufferDirty);

  AsyncMcClient& getAsyncMcClient();
  void initializeAsyncMcClient();

  ProxyDestination(
      ProxyBase& proxy,
      std::shared_ptr<AccessPoint> ap,
      std::chrono::milliseconds timeout,
      uint64_t qosClass,
      uint64_t qosPath,
      folly::StringPiece routerInfoName);

  void onTkoEvent(TkoLogEvent event, mc_res_t result) const;

  void handleRxmittingConnection(const mc_res_t result, uint64_t latency);

  bool latencyAboveThreshold(uint64_t latency);

  void onTransitionToState(State state);
  void onTransitionFromState(State state);
  void onTransitionImpl(State state, bool to);

  void* stateList_{nullptr};
  folly::IntrusiveListHook stateListHook_;

  std::weak_ptr<ProxyDestination> selfPtr_;

  friend class ProxyDestinationMap;
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook

#include "ProxyDestination-inl.h"
