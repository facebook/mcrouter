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

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <folly/experimental/StringKeyedUnorderedMap.h>
#include <folly/Range.h>

#include "mcrouter/TkoCounters.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyDestination;
class TkoTrackerMap;

/**
 * We record the number of consecutive failures for each destination.
 * Once it goes over a certain threshold, we mark the destination as TKO
 * (stands for total knockout :), meaning that we should not send regular
 * requests to it.  The calling code can use this status to start sending probe
 * requests and only unmark it as TKO once a probe comes back successfully.
 *
 * We distinguish between soft and hard TKOs.  Hard TKOs imply that a box cannot
 * be connected to, and results in an instant failure.  We can have an unlimited
 * number of these. Soft TKOs are the result of soft errors and timeouts, as
 * described above.  These may be limited in number.  A box may transition for
 * soft to hard TKO, but once hard TKO the box must send a successful reply to
 * be unmarked.
 *
 * Perf implications: recordSuccess() with no previous failures and isTko()
 * are lock-free, so the common (no error results) path is fast.
 *
 * Races are not a big issue: it's OK to miss a few events. What's of critical
 * importance is that once a destination is marked TKO, only the responsible
 * proxy (the one sending probes) can change its TKO state. Once we are in TKO
 * the responsible thread effectively has a mutex over all state in TkoTracker,
 * and so races aren't possible.
 */
class TkoTracker {
 public:
  /**
   * @return Is the destination currently marked Hard TKO?
   */
  bool isHardTko() const;

  /**
   * @return Is the destination currently marked Soft TKO?
   */
  bool isSoftTko() const;

  /**
   * @return Is the destination currently marked TKO?
   */
  bool isTko() const {
    return sumFailures_ > tkoThreshold_;
  }

  /**
   * @return current number of consecutive failures.
   *         This is basically a number of recordHardFailure/recordSoftFailure
   *         calls after last recordSuccess.
   */
  size_t consecutiveFailureCount() const {
    return consecutiveFailureCount_;
  }

  /**
   * @return number of TKO destinations for current router
   */
  const TkoCounters& globalTkos() const;

  /**
   * Can be called from any proxy thread.
   * Signal that a "soft" failure occurred. We need to see
   * tko_threshold "soft" failures in a row to mark a host TKO. Will not TKO
   * a host if currentSoftTkos would exceed maxSoftTkos
   *
   * @param pdstn  a pointer to the calling proxydestination for tracking
   *               responsibility.
   *
   * @return true if we just reached tko_threshold with this result,
   *         marking the host TKO.  In this case, the calling proxy
   *         is responsible for sending probes and calling recordSuccess()
   *         once a probe is successful.
   */
  bool recordSoftFailure(ProxyDestination* pdstn);

  /**
   * Can be called from any proxy thread.
   * Signal that a "hard" failure occurred - marks the host TKO
   * right away.
   *
   * @param pdstn  a pointer to the calling proxydestination for tracking
   *               responsibility.
   *
   * @return true if we just reached tko_threshold with this result,
   *         marking the host TKO.  In this case, the calling proxy
   *         is responsible for sending probes and calling recordSuccess()
   *         once a probe is successful. Note, transition from soft to hard
   *         TKO does not result in a change of responsibility.
   */
  bool recordHardFailure(ProxyDestination* pdstn);

  /**
   * Resets all consecutive failures accumulated so far
   * (unmarking any TKO status).
   *
   * @param pdstn  a pointer to the calling proxydestination for tracking
   *               responsibility.
   *
   * @return true if `pdstn` was responsible for sending probes
   */
  bool recordSuccess(ProxyDestination* pdstn);

  /**
   * Should be called when ProxyDestination is going to be destroyed
   *
   * @return true if `pdstn` was responsible for sending probes
   */
  bool removeDestination(ProxyDestination* pdstn);

  ~TkoTracker();
 private:
  // The string is stored in TkoTrackerMap::trackers_
  folly::StringPiece key_;
  const size_t tkoThreshold_;
  const size_t maxSoftTkos_;
  TkoTrackerMap& trackerMap_;

  /* sumFailures_ is used for a few things depending on the state of the
     destination. For a destination that is not TKO, it tracks the number of
     consecutive soft failures to a destination.
     If a destination is soft TKO, it contains the numerical representation of
     the pointer to the proxy thread that is responsible for sending it probes.
     If a destination is hard TKO, it contains the same value as for soft TKO,
     but with the LSB set to 1 instead of 0.
     In summary, allowed values are:
       0, 1, .., tkoThreshold_ - 1, pdstn, pdstn | 0x1, where pdstn is the
       address of any of the proxy threads for this destination. */
  std::atomic<uintptr_t> sumFailures_{0};

  std::atomic<size_t> consecutiveFailureCount_{0};

  /* Decrement the global counter of current soft TKOs. */
  void decrementSoftTkoCount();
  /* Attempt to increment the global counter of current soft TKOs. Return true
     if successful and false if limits have been reached. */
  bool incrementSoftTkoCount();

  /* Modifies the value of sumFailures atomically. Fails only
     in the case that another proxy takes responsibility, in which case all
     other proxies may not modify state */
  bool setSumFailures(uintptr_t value);

  /* Return true if this thread is responsible for the TKO state */
  bool isResponsible(ProxyDestination* pdstn) const;

  /**
   * @param tkoThreshold require this many soft failures to mark
   *        the destination TKO
   * @param maxSoftTkos the maximum number of concurrent soft TKOs allowed in
   *        the router
   * @param globalTkoStats number of TKO destination for current router
   */
  TkoTracker(size_t tkoThreshold,
             size_t maxSoftTkos,
             TkoTrackerMap& trackerMap);

  friend class TkoTrackerMap;
};

/**
 * Manages the TkoTrackers for all servers
 */
class TkoTrackerMap {
 public:
  TkoTrackerMap() = default;
  TkoTrackerMap(const TkoTrackerMap&) = delete;
  TkoTrackerMap(TkoTrackerMap&&) = delete;

  /**
   * Creates/updates TkoTracker for `pdstn` and updates `pdstn->tko` pointer.
   */
  void updateTracker(ProxyDestination& pdstn,
                     const size_t tkoThreshold,
                     const size_t maxSoftTkos);

  /**
   * @return  number of servers that recently returned error replies.
   */
  size_t getSuspectServersCount();

  /**
   * @return  servers that recently returned error replies.
   *   Format: {
   *     server ip => ( is server marked as TKO?, number of failures )
   *   }
   *   Only servers with positive number of failures will be returned.
   */
  std::unordered_map<std::string, std::pair<bool, size_t>> getSuspectServers();

  const TkoCounters& globalTkos() const {
    return globalTkos_;
  }

  std::weak_ptr<TkoTracker> getTracker(folly::StringPiece key);
 private:
  std::mutex mx_;
  folly::StringKeyedUnorderedMap<std::weak_ptr<TkoTracker>> trackers_;

  // Total number of boxes marked as TKO.
  TkoCounters globalTkos_;

  void removeTracker(folly::StringPiece key) noexcept;

  friend class TkoTracker;
};

}}} // facebook::memcache::mcrouter
