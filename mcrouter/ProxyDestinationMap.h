/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

using asox_timer_t = void*;

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyClientCommon;
class ProxyDestination;
class proxy_t;

/**
 * Represents a set of aggregated stats across all destinations in a map.
 */
struct AggregatedDestinationStats {
  // Number of requests pending to be sent over network.
  uint64_t pendingRequests{0};
  // Number of requests been sent over network or already waiting for reply.
  uint64_t inflightRequests{0};
  // Average batches size in form of (num_request, num_batches).
  // This value is based on some subset of most recent requests (each
  // destination maintains its own window).
  // See AsyncMcClient::getBatchingStat() for more details.
  std::pair<uint64_t, uint64_t> batches{0, 0};
};

/**
 * Manages lifetime of ProxyDestinations. Main goal is to reuse same
 * ProxyDestinations (thus do not close opened connecitons) during
 * router reconfigururation.
 *
 * ProxyDestination can be either 'used' ot 'unused'. 'Used' means it is present
 * in current configuration. 'Unused' means it is not present in current
 * configuration (e.g. pool was removed and mcrouter reconfigured).
 *
 * Also ProxyDestination can be 'active' or 'inactive'. 'Active' means there is
 * opened connection to this destination and there were requests during last
 * reset_inactive_connection_interval ms routed to this destination.
 * 'Inactive' means there were no requests and connection may be closed.
 */
class ProxyDestinationMap {
 public:
  explicit ProxyDestinationMap(proxy_t* proxy);

  /**
   * Removes all ProxyDestinations that are used only by ProxyDestinationMap
   * (shared_ptr is unique)
   */
  void removeAllUnused();

  /**
   * If ProxyDestination is already stored in this object - returns it;
   * otherwise creates a new one.
   */
  std::shared_ptr<ProxyDestination> fetch(const ProxyClientCommon& client);

  /**
   * @return  a set of stats across all destinations (see
   *          AggregatedDestinationStats).
   */
  AggregatedDestinationStats getDestinationsStats();

  /**
   * Mark destination as 'active', so it won't be closed on next
   * resetAllInactive call
   */
  void markAsActive(ProxyDestination& destination);

  /**
   * Close all 'inactive' destinations i.e. destinations which weren't marked
   * 'active' after last removeAllInactive call.
   */
  void resetAllInactive();

  /**
   * Set timer which resets inactive connections.
   * @param interval timer interval, should be greater than zero.
   */
  void setResetTimer(std::chrono::milliseconds interval);

  ~ProxyDestinationMap();

 private:
  struct StateList;

  proxy_t* proxy_;
  std::unordered_map<std::string, std::shared_ptr<ProxyDestination>>
    destinations_;
  std::mutex destinationsLock_;

  std::unique_ptr<StateList> active_;
  std::unique_ptr<StateList> inactive_;

  asox_timer_t resetTimer_;
};

}}} // facebook::memcache::mcrouter
