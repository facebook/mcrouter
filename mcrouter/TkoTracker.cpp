/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "TkoTracker.h"

#include <cassert>

#include <glog/logging.h>

#include "mcrouter/ProxyDestination.h"
#include "mcrouter/TkoCounters.h"

namespace facebook { namespace memcache { namespace mcrouter {

TkoTracker::TkoTracker(size_t tkoThreshold,
                       size_t maxSoftTkos,
                       TkoCounters& globalTkoCounters)
  : tkoThreshold_(tkoThreshold),
    maxSoftTkos_(maxSoftTkos),
    globalTkos_(globalTkoCounters) {
  }

bool TkoTracker::isHardTko() const {
  uintptr_t curSumFailures = sumFailures_;
  return (curSumFailures > tkoThreshold_ && curSumFailures % 2 == 1);
}

bool TkoTracker::isSoftTko() const {
  uintptr_t curSumFailures = sumFailures_;
  return (curSumFailures > tkoThreshold_ && curSumFailures % 2 == 0);
}

const TkoCounters& TkoTracker::globalTkos() const {
  return globalTkos_;
}

bool TkoTracker::incrementSoftTkoCount() {
  size_t old_soft_tkos = globalTkos_.softTkos;
  do {
    assert(old_soft_tkos <= maxSoftTkos_);
    if (old_soft_tkos == maxSoftTkos_) {
      /* We've hit the soft TKO limit and can't tko this box. */
      return false;
    }
  } while (!globalTkos_.softTkos.compare_exchange_weak(old_soft_tkos,
                                                       old_soft_tkos + 1));
  return true;
}

void TkoTracker::decrementSoftTkoCount() {
  // Decrement the counter and ensure we haven't gone below 0
  size_t old_soft_tkos = globalTkos_.softTkos.fetch_sub(1);
  assert(old_soft_tkos != 0);
}

bool TkoTracker::setSumFailures(uintptr_t value) {
  uintptr_t curSumFailures = sumFailures_;
  do {
    /* If the destination is TKO but we're not responsible we can't change
       state, so return. */
    if (curSumFailures > tkoThreshold_) {
      return false;
    }
  } while (!sumFailures_.compare_exchange_weak(curSumFailures, value));
  return true;
}

bool TkoTracker::recordSoftFailure(ProxyDestination* pdstn) {
  ++consecutiveFailureCount_;

  /* We increment soft tko count first before actually taking responsibility
     for the TKO. This means we run the risk that multiple proxies
     increment the count for the same destination, causing us to be overly
     conservative. Eventually this will get corrected, as only one proxy can
     ever mark it TKO, but we may be inconsistent for a very short time.
  */
  /* If host is in any state of TKO, we just leave it alone */
  if (isTko()) {
    return false;
  }

  uintptr_t curSumFailures = sumFailures_;
  uintptr_t value = 0;
  uintptr_t pdstnAddr = reinterpret_cast<uintptr_t>(pdstn);
  do {
    /* If we're one failure below the limit, we're about to enter softTKO */
    if (curSumFailures == tkoThreshold_ - 1) {
    /* If we're not allowed to TKO the box, leave it one hit away
       Note, we need to check value to ensure we didn't already increment the
       counter in a previous iteration */
      if (value != pdstnAddr && !incrementSoftTkoCount()) {
        return false;
      }
      value = pdstnAddr;
    } else {
      if (value == pdstnAddr) {
        /* a previous loop iteration attempted to soft TKO the box,
         so we need to undo that */
        decrementSoftTkoCount();
      }
      /* Someone else is responsible, so quit */
      if (curSumFailures > tkoThreshold_) {
        return false;
      } else {
        value = curSumFailures + 1;
      }
    }
  } while (!sumFailures_.compare_exchange_weak(curSumFailures, value));
  return value == pdstnAddr;
}

bool TkoTracker::recordHardFailure(ProxyDestination* pdstn) {
  ++consecutiveFailureCount_;

  if (isHardTko()) {
    return false;
  }
  /* If we were already TKO and responsible, but not hard TKO, it means we were
     in soft TKO before. We need decrement the counter and convert to hard
     TKO */
  if (isResponsible(pdstn)) {
    /* convert to hard failure */
    sumFailures_ |= 1;
    decrementSoftTkoCount();
    ++globalTkos_.hardTkos;
    /* We've already been marked responsible */
    return false;
  }

  // If the call below succeeds we marked the box TKO and took responsibility.
  bool success = setSumFailures(reinterpret_cast<uintptr_t>(pdstn) | 1);
  if (success) {
    ++globalTkos_.hardTkos;
  }
  return success;
}

bool TkoTracker::isResponsible(ProxyDestination* pdstn) const {
  return (sumFailures_ & ~1) == reinterpret_cast<uintptr_t>(pdstn);
}

void TkoTracker::recordSuccess(ProxyDestination* pdstn) {
  /* If we're responsible, no one else can change any state and we're
     effectively under mutex. */
  if (isResponsible(pdstn)) {
    /* Coming out of TKO, we need to decrement counters */
    if (isSoftTko()) {
       decrementSoftTkoCount();
    }
    if (isHardTko()) {
      --globalTkos_.hardTkos;
    }
    sumFailures_ = 0;
  } else {
    setSumFailures(0);
  }

  consecutiveFailureCount_ = 0;
}

}}} // facebook::memcache::mcrouter
