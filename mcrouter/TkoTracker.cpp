/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "TkoTracker.h"

#include <cassert>
#include <glog/logging.h>

#include "mcrouter/ProxyDestination.h"

namespace facebook { namespace memcache { namespace mcrouter {

TkoTracker::TkoTracker(size_t tkoThreshold,
                       size_t maxSoftTkos,
                       std::atomic<size_t>& currentSoftTkos)
  : tkoThreshold_(tkoThreshold),
    maxSoftTkos_(maxSoftTkos),
    currentSoftTkos_(currentSoftTkos),
  /* Internally, when a box is TKO we store a proxy address rather than a
     count. If the box is soft TKO, the LSB is 0. Otherwise it's 1. */
    sumFailures_(0) {
  }

bool TkoTracker::isHardTko() const {
  uintptr_t curSumFailures = sumFailures_;
  return (curSumFailures > tkoThreshold_ && curSumFailures % 2 == 1);
}

bool TkoTracker::isSoftTko() const {
  uintptr_t curSumFailures = sumFailures_;
  return (curSumFailures > tkoThreshold_ && curSumFailures % 2 == 0);
}

bool TkoTracker::incrementSoftTkoCount() {
  size_t old_soft_tkos = currentSoftTkos_;
  do {
    assert(old_soft_tkos <= maxSoftTkos_);
    if (old_soft_tkos == maxSoftTkos_) {
      /* We've hit the soft TKO limit and can't tko this box. */
      return false;
    }
  } while (!currentSoftTkos_.compare_exchange_weak(old_soft_tkos,
                                                   old_soft_tkos + 1));
  VLOG(1) << old_soft_tkos + 1 << " destinations marked soft TKO";
  return true;
}

void TkoTracker::decrementSoftTkoCount() {
  // Decrement the counter and ensure we haven't gone below 0
  size_t old_soft_tkos = currentSoftTkos_.fetch_sub(1);
  assert(old_soft_tkos != 0);
  VLOG(1) << old_soft_tkos - 1 << " destinations marked soft TKO";
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
    /* We've already been marked responsible */
    return false;
  }

  /* If the call below succeeds we marked the box TKO and took responsibility.
  */
  return setSumFailures(reinterpret_cast<uintptr_t>(pdstn) | 1);
}

bool TkoTracker::isResponsible(ProxyDestination* pdstn) {
  return (sumFailures_ & ~1) == reinterpret_cast<uintptr_t>(pdstn);
}

void TkoTracker::recordSuccess(ProxyDestination* pdstn) {
  /* If we're responsible, no one else can change any state and we're
     effectively under mutex. */
  if (isResponsible(pdstn)) {
    /* If we're coming out of softTKO, we need to decrement the counter */
    bool unmark = isSoftTko();
    sumFailures_ = 0;
    if (unmark) {
       decrementSoftTkoCount();
    }
  } else {
    setSumFailures(0);
  }
}
}}} // facebook::memcache::mcrouter
