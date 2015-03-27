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
#include <string>

#include <folly/IntrusiveList.h>

#include "mcrouter/lib/network/AccessPoint.h"
#include "mcrouter/lib/network/AsyncMcClient.h"
#include "mcrouter/config.h"
#include "mcrouter/ExponentialSmoothData.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/TkoLog.h"

using asox_timer_t = void*;

namespace facebook { namespace memcache {

class McReply;
class McRequest;

namespace mcrouter {

class DestinationRequestCtx;
class ProxyClientCommon;
class ProxyDestinationMap;
class TkoTracker;
class proxy_t;

class ProxyDestination {
 public:
  enum class State {
    kNew,           // never connected
    kUp,            // currently connected
    kDown,          // currently down
    kClosed,        // closed due to inactive
    kNumStates
  };

  struct Stats {
    State state{State::kNew};
    ExponentialSmoothData avgLatency;
    uint64_t results[mc_nres] = {0};
    size_t probesSent{0};

    explicit Stats(const McrouterOptions& opts);
  };

  proxy_t* proxy{nullptr}; ///< for convenience
  const AccessPoint accessPoint;
  const std::string pdstnKey;///< consists of ap, server_timeout

  std::shared_ptr<TkoTracker> tracker;

  static std::shared_ptr<ProxyDestination> create(proxy_t* proxy,
                                                  const ProxyClientCommon& ro,
                                                  std::string pdstnKey);

  ~ProxyDestination();

  // This is a blocking call that will return reply, once it's ready.
  template <int Op, class Request>
  typename ReplyType<McOperation<Op>, Request>::type
  send(const Request& request, McOperation<Op>, DestinationRequestCtx& req_ctx,
       std::chrono::milliseconds timeout);
  // returns true if okay to send req using this client
  bool may_send() const;

  /**
   * @return stats for ProxyDestination
   */
  const Stats& stats() const {
    return stats_;
  }

  void resetInactive();

  size_t getPendingRequestCount() const;
  size_t getInflightRequestCount() const;

  /**
   * Get average request batch size that is sent over network in one write.
   *
   * See AsyncMcClient::getBatchingStat for more details.
   */
  std::pair<uint64_t, uint64_t> getBatchingStat() const;

  void updateShortestTimeout(std::chrono::milliseconds timeout);

  void updatePoolName(std::string poolName) {
    poolName_ = std::move(poolName);
  }

 private:
  static const uint64_t kDeadBeef = 0xdeadbeefdeadbeefULL;

  std::unique_ptr<AsyncMcClient> client_;

  // Shortest timeout among all ProxyClientCommon's using this destination
  std::chrono::milliseconds shortestTimeout_{0};
  const bool useSsl_{false};
  const uint64_t qos_{0};
  uint64_t magic_{0}; ///< to allow asserts that pdstn is still alive

  Stats stats_;

  int probe_delay_next_ms{0};
  std::unique_ptr<McRequest> probe_req;
  asox_timer_t probe_timer{nullptr};
  std::string poolName_;

  void setState(State st);

  void start_sending_probes();
  void stop_sending_probes();

  void schedule_next_probe();

  void handle_tko(const McReply& reply, bool is_probe_req);

  // on probe timer
  void on_timer(const asox_timer_t timer);

  // Process tko, stats and duration timer.
  void onReply(const McReply& reply, DestinationRequestCtx& destreqCtx);

  AsyncMcClient& getAsyncMcClient();
  void initializeAsyncMcClient();

  ProxyDestination(proxy_t* proxy,
                   const ProxyClientCommon& ro,
                   std::string pdstnKey);

  void onTkoEvent(TkoLogEvent event, mc_res_t result) const;

  void* stateList_{nullptr};
  folly::IntrusiveListHook stateListHook_;

  std::weak_ptr<ProxyDestination> selfPtr_;

  friend class ProxyDestinationMap;
};

}}}  // facebook::memcache::mcrouter

#include "ProxyDestination-inl.h"
