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
 * Races between lock-free read path and locked write path are not an issue:
 * it means that different threads might have temporarily inconsistent view
 * of TKO status.  Within each thread, any sequence of calls to
 * isTko(), *Failure(), recordSuccess() will result in a consistent view.
 * In the longer, we're guaranteed to unmark the host since there's exactly
 * one probe-sending thread.
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
  explicit TkoTracker(size_t tkoThreshold,
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
    return sumFailures_ >= tkoThreshold_;
  }


  /**
   * Can be called from any proxy thread.
   * Signal that a "soft" failure occurred. We need to see
   * tko_threshold "soft" failures in a row to mark a host TKO. Will not TKO
   * a host if currentSoftTkos would exceed maxSoftTkos
   *
   * @return true if we just reached tko_threshold with this result,
   *         marking the host TKO.  In this case, the calling proxy
   *         is responsible for sending probes and calling recordSuccess()
   *         once a probe is successful.
   */
  bool recordSoftFailure();

  /**
   * Can be called from any proxy thread.
   * Signal that a "hard" failure occurred - marks the host TKO
   * right away.
   *
   * @return true if we just reached tko_threshold with this result,
   *         marking the host TKO.  In this case, the calling proxy
   *         is responsible for sending probes and calling recordSuccess()
   *         once a probe is successful. Note, transition from soft to hard
   *         TKO does not result in a change of responsibility.
   */
  bool recordHardFailure();

  /**
   * Resets all consecutive failures accumulated so far
   * (unmarking any TKO status).
   */
  void recordSuccess();

 private:
  const size_t tkoThreshold_;
  const size_t maxSoftTkos_;
  std::atomic<size_t>& currentSoftTkos_;
  std::atomic<size_t> sumFailures_;
  std::mutex mx_;

  /* Decrement the global counter of current soft TKOs. */
  void decrementSoftTkoCount();
  /* Attempt to increment the global counter of current soft TKOs. Return true
     if successful and false if limits have been reached. */
  bool incrementSoftTkoCount();
};

}}} // facebook::memcache::mcrouter
