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
class ProxyDestination;

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
  explicit ProxyDestinationMap(ProxyBase* proxy);

  /**
   * If ProxyDestination is already stored in this object - returns it;
   * otherwise, returns nullptr.
   */
  std::shared_ptr<ProxyDestination> find(
      const AccessPoint& ap,
      std::chrono::milliseconds timeout) const;
  /**
   * If ProxyDestination is already stored in this object - returns it;
   * otherwise creates a new one.
   */
  std::shared_ptr<ProxyDestination> emplace(
      std::shared_ptr<AccessPoint> ap,
      std::chrono::milliseconds timeout,
      uint64_t qosClass,
      uint64_t qosPath,
      folly::StringPiece routerInfoName);

  /**
   * Remove destination from both active and inactive lists
   */
  void removeDestination(ProxyDestination& destination);

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

  /**
   * Calls f(Key, const ProxyDestination&) for each destination stored
   * in ProxyDestinationMap. The whole map is locked during the call.
   *
   * TODO: replace with getStats()
   */
  template <typename Func>
  void foreachDestinationSynced(Func&& f) {
    // The toFree vector is used to delay destruction as we have the following
    // race condition: ProxyDestination destructor will try to grab a lock
    // on destionationsLock_, which is already locked here.
    std::vector<std::shared_ptr<const ProxyDestination>> toFree;
    {
      std::lock_guard<std::mutex> lock(destinationsLock_);
      for (auto& it : destinations_) {
        if (std::shared_ptr<const ProxyDestination> d = it.second.lock()) {
          f(it.first, *d);
          toFree.push_back(std::move(d));
        }
      }
    }
  }

  ~ProxyDestinationMap();

 private:
  struct StateList;

  ProxyBase* proxy_;
  folly::StringKeyedUnorderedMap<std::weak_ptr<ProxyDestination>> destinations_;
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
  std::shared_ptr<ProxyDestination> find(const std::string& key) const;

  /**
   * Schedules timeout for resetting inactive connections.
   *
   * @param initial  true iff this an initial attempt to schedule timer.
   */
  void scheduleTimer(bool initialAttempt);
};
} // namespace mcrouter
} // namespace memcache
} // namespace facebook
