/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
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
  };

  ProxyBase& proxy; // for convenience

  std::shared_ptr<TkoTracker> tracker;

  ~ProxyDestination();

  // This is a blocking call that will return reply, once it's ready.
  template <class Request>
  ReplyT<Request> send(
      const Request& request,
      DestinationRequestCtx& requestContext,
      std::chrono::milliseconds timeout,
      ReplyStatsContext& replyStatsContext);

  // returns true if okay to send req using this client
  bool may_send() const;

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

  void start_sending_probes();
  void stop_sending_probes();

  void schedule_next_probe();

  void handle_tko(const mc_res_t result, bool is_probe_req);

  // Process tko, stats and duration timer.
  void onReply(
      const mc_res_t result,
      DestinationRequestCtx& destreqCtx,
      const ReplyStatsContext& replyStatsContext);

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

  void handleRxmittingConnection();

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
