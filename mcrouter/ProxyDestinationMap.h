/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <folly/Range.h>
#include <folly/experimental/StringKeyedUnorderedMap.h>
#include <folly/io/async/AsyncTimeout.h>

namespace facebook {
namespace memcache {

struct AccessPoint;

namespace mcrouter {

class ProxyBase;
template <class Transport>
class ProxyDestination;
class ProxyDestinationBase;

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
 *
 * Note: There's one ProxyDestinationMap per proxy thread.
 */
class ProxyDestinationMap {
 public:
  explicit ProxyDestinationMap(ProxyBase* proxy);

  /**
   * If ProxyDestination is already stored in this object - returns it;
   * otherwise, returns nullptr.
   */
  template <class Transport>
  std::shared_ptr<ProxyDestination<Transport>> find(
      const AccessPoint& ap,
      std::chrono::milliseconds timeout) const;
  /**
   * If ProxyDestination is already stored in this object - returns it;
   * otherwise creates a new one.
   *
   * @throws std::logic_error If Transport is not compatible with
   *                          AccessPoint::getProtocol().
   */
  template <class Transport>
  std::shared_ptr<ProxyDestination<Transport>> emplace(
      std::shared_ptr<AccessPoint> ap,
      std::chrono::milliseconds timeout,
      uint64_t qosClass,
      uint64_t qosPath,
      folly::StringPiece routerInfoName);

  /**
   * Remove destination from both active and inactive lists
   */
  void removeDestination(ProxyDestinationBase& destination);

  /**
   * Mark destination as 'active', so it won't be closed on next
   * resetAllInactive call
   */
  void markAsActive(ProxyDestinationBase& destination);

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

  /**
   * Calls f(Key, const ProxyDestination&) for each destination stored
   * in ProxyDestinationMap. The whole map is locked during the call.
   *
   * TODO: replace with getStats()
   */
  template <typename Func>
  void foreachDestinationSynced(Func&& f) {
    std::lock_guard<std::mutex> lock(destinationsLock_);
    for (auto& it : destinations_) {
      if (std::shared_ptr<const ProxyDestinationBase> dst = it.second.lock()) {
        f(it.first, *dst);
        // Disposal of the ProxyDestination object should execute on the proxy
        // thread to prevent races in accessing members of ProxyDestination
        // that are only accessed within eventbase-executed methods.
        releaseProxyDestinationRef(std::move(dst));
      }
    }
  }

  ~ProxyDestinationMap();

 private:
  struct StateList;

  ProxyBase* proxy_;
  folly::StringKeyedUnorderedMap<std::weak_ptr<ProxyDestinationBase>>
      destinations_;
  mutable std::mutex destinationsLock_;

  std::unique_ptr<StateList> active_;
  std::unique_ptr<StateList> inactive_;

  uint32_t inactivityTimeout_;
  std::unique_ptr<folly::AsyncTimeout> resetTimer_;

  /**
   * If ProxyDestination is already stored in this object - returns it;
   * otherwise, returns nullptr.
   * Note: caller must be holding destionationsLock_.
   */
  std::shared_ptr<ProxyDestinationBase> find(const std::string& key) const;

  /**
   * Schedules timeout for resetting inactive connections.
   *
   * @param initial  true iff this an initial attempt to schedule timer.
   */
  void scheduleTimer(bool initialAttempt);

  /**
   * Generates the key to be used in this map for a given (pdst, timeout) pair.
   */
  std::string genProxyDestinationKey(
      const AccessPoint& ap,
      std::chrono::milliseconds timeout) const;

  /*
   * Releases the shared_ptr reference on the destination's event-base.
   */
  static void releaseProxyDestinationRef(
      std::shared_ptr<const ProxyDestinationBase>&& destination);
};

} // namespace mcrouter
} // namespace memcache
} // namespace facebook

#include "mcrouter/ProxyDestinationMap-inl.h"
