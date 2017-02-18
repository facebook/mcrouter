/*
 *  Copyright (c) 2017, Facebook, Inc.
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
#include <folly/SpinLock.h>

#include "mcrouter/AsyncTimer.h"
#include "mcrouter/ExponentialSmoothData.h"
#include "mcrouter/TkoLog.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/AccessPoint.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/lib/network/gen/Memcache.h"

namespace facebook {
namespace memcache {

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

  ProxyBase* proxy{nullptr}; ///< for convenience

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

  void updatePoolName(std::string poolName) {
    poolName_ = std::move(poolName);
  }

 private:
  static const uint64_t kDeadBeef = 0xdeadbeefdeadbeefULL;

  std::unique_ptr<AsyncMcClient> client_;
  std::shared_ptr<const AccessPoint> accessPoint_;
  mutable folly::SpinLock clientLock_; // AsyncMcClient lock for stats threads.

  // Shortest timeout among all DestinationRoutes using this destination
  std::chrono::milliseconds shortestTimeout_{0};
  const uint64_t qosClass_{0};
  const uint64_t qosPath_{0};
  std::string routerInfoName_;
  uint64_t magic_{0}; ///< to allow asserts that pdstn is still alive

  Stats stats_;

  uint64_t lastRetransCycles_{0}; // Cycles when restransmits were last fetched
  uint64_t rxmitsToCloseConnection_{0};
  uint64_t lastConnCloseCycles_{0}; // Cycles when connection was last closed

  int probe_delay_next_ms{0};
  std::unique_ptr<McVersionRequest> probe_req;
  std::string poolName_;
  // The string is stored in ProxyDestinationMap::destinations_
  folly::StringPiece pdstnKey_; ///< consists of ap, server_timeout

  AsyncTimer<ProxyDestination> probeTimer_;

  static std::shared_ptr<ProxyDestination> create(
      ProxyBase& proxy,
      std::shared_ptr<AccessPoint> ap,
      std::chrono::milliseconds timeout,
      uint64_t qosClass,
      uint64_t qosPath,
      std::string routerInfoName);

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
      std::string routerInfoName);

  void onTkoEvent(TkoLogEvent event, mc_res_t result) const;

  void timerCallback();

  void handleRxmittingConnection();

  void* stateList_{nullptr};
  folly::IntrusiveListHook stateListHook_;

  std::weak_ptr<ProxyDestination> selfPtr_;

  friend class ProxyDestinationMap;
  friend class AsyncTimer<ProxyDestination>;
};
}
}
} // facebook::memcache::mcrouter

#include "ProxyDestination-inl.h"
