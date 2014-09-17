/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <atomic>
#include <mutex>

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyDestination;

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
   * @param tkoThreshold require this many soft failures to mark
   *        the destination TKO
   * @param maxSoftTkos the maximum number of concurrent soft TKOs allowed in
   *        the router
   * @param currentSoftTkos the number of current soft TKOs
   */
  TkoTracker(size_t tkoThreshold,
             size_t maxSoftTkos,
             std::atomic<size_t>& currentSoftTkos);

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
   * @return number of destinations marked Soft TKO for current router
   */
  size_t globalSoftTkos() const;

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
   */
  void recordSuccess(ProxyDestination* pdstn);

 private:
  const size_t tkoThreshold_;
  const size_t maxSoftTkos_;
  std::atomic<size_t>& currentSoftTkos_;
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
  std::atomic<uintptr_t> sumFailures_;

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
  bool isResponsible(ProxyDestination* pdstn);
};

}}} // facebook::memcache::mcrouter
