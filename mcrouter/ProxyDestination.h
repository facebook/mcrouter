/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <sys/time.h>

#include <atomic>
#include <memory>
#include <string>

#include "folly/IntrusiveList.h"
#include "mcrouter/AccessPoint.h"
#include "mcrouter/config.h"

using asox_timer_t = void*;
class fb_timer_s;
using fb_timer_t = fb_timer_s;

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyClientCommon;
class ProxyClientOwner;
class ProxyClientShared;
class ProxyDestinationMap;
class dynamic_stat_t;
class proxy_t;

enum proxy_client_state_t {
  PROXY_CLIENT_UNKNOWN = 0,   // bug
  PROXY_CLIENT_NEW,           // never connected
  PROXY_CLIENT_UP,            // currently connected
  PROXY_CLIENT_TKO,           // waiting for retry timeout
  PROXY_CLIENT_NUM_STATES
};

struct proxy_client_conn_stats_t {
  char is_up;
  fb_timer_t* rtt_timer;
  uint64_t results[mc_nres];

  proxy_client_conn_stats_t();
};

struct ProxyDestination {
  static const uint64_t kDeadBeef = 0xdeadbeefdeadbeefULL;

  proxy_t* proxy{nullptr}; ///< for convenience
  AccessPoint accessPoint;
  std::string destinationKey;///< always the same for a given (host, port)
  int rxpriority{0};
  int txpriority{0};
  const timeval_t server_timeout{0};
  const std::string pdstnKey;///< consists of ap, server_timeout
  uint64_t magic{0}; ///< to allow asserts that pdstn is still alive
  uint64_t proxy_magic{0}; ///< to allow asserts that proxy is still alive

  ProxyClientOwner* owner{nullptr};
  std::shared_ptr<ProxyClientShared> shared;

  /**
   * A key that would be hashed to this client in the current config.
   * Set on the first request and reset with reconfigure.
   * The client creates a copy of the key and is responsible for it.
   */
  std::string sample_key;

  proxy_client_conn_stats_t stats;
  dynamic_stat_t* stats_ptr{nullptr};

  bool use_ssl{false};

  static std::shared_ptr<ProxyDestination> create(proxy_t* proxy,
                                                   const ProxyClientCommon& ro,
                                                   std::string pdstnKey);

  ~ProxyDestination();

  void track_latency(int64_t latency);
  void handle_tko(mc_res_t result, const mc_msg_t* reply,
                  int consecutive_errors);

  // returns non-zero on error
  int send(mc_msg_t* request, void* req_ctx, uint64_t senderId);
  // returns 1 if okay to send req using this client
  int may_send(mc_msg_t *req);

  proxy_client_state_t state();

  void resetInactive();

  void on_up();
  void on_down();
  void on_reply(mc_msg_t *req,
                mc_msg_t *reply, mc_res_t result, void* req_ctx);

  // on probe timer
  void on_timer(const asox_timer_t timer);

  size_t getPendingRequestCount() const;
  size_t getInflightRequestCount() const;

 private:
  std::unique_ptr<DestinationMcClient> client_;

  int probe_delay_next_ms{0};
  bool sending_probes{false};
  mc_msg_t* probe_req{nullptr};
  asox_timer_t probe_timer{nullptr};
  size_t consecutiveErrors_{0};
  double avgLatency_{0.0};

  char resetting{0}; // If 1 when inside on_down, the call was due to a forced
                     // mc_client_reset and not a remote connection failure.

  timeval_t up_time{0};
  timeval_t down_time{0};

  // tko behaviour
  char marked_tko{0};

  void start_sending_probes();
  void schedule_next_probe();
  void stop_sending_probes();

  void reset_fields();

  void mark_tko();
  void unmark_tko();
  void unmark_global_tko();

  void initializeClient();

  ProxyDestination(proxy_t* proxy,
                 const ProxyClientCommon& ro,
                 std::string pdstnKey);

  // for no-network mode (debug/performance measurement only)
  void sendFakeReply(mc_msg_t* request, void* req_ctx);

  std::atomic<bool> isUsedInConfig_{false};
  void* stateList_{nullptr};
  folly::IntrusiveListHook stateListHook_;

  std::weak_ptr<ProxyDestination> selfPtr_;

  friend class ProxyDestinationMap;
};

}}}  // facebook::memcache::mcrouter
