#include "TkoTracker.h"

#include <cassert>
#include <glog/logging.h>

namespace facebook { namespace memcache { namespace mcrouter {

TkoTracker::TkoTracker(size_t tkoThreshold,
                       size_t maxSoftTkos,
                       std::atomic<size_t>& currentSoftTkos)
  : tkoThreshold_(tkoThreshold),
    maxSoftTkos_(maxSoftTkos),
    currentSoftTkos_(currentSoftTkos),
  /* Internally, we use tkoThreshold_ + 1 as a sentinel for hard TKO */
    sumFailures_(0) {
  }

bool TkoTracker::isHardTko() const {
  assert(sumFailures_ <= tkoThreshold_ + 1);
  return sumFailures_ == tkoThreshold_ + 1;
}

bool TkoTracker::isSoftTko() const {
  assert(sumFailures_ <= tkoThreshold_ + 1);
  return sumFailures_ == tkoThreshold_;
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

bool TkoTracker::recordSoftFailure() {
  std::lock_guard<std::mutex> lock(mx_);
  /* If host is in any state of TKO, we just leave it alone */
  if (isTko()) {
    return false;
  }
  /* If we're one failure below the limit, we're about to enter softTKO */
  if (sumFailures_ == tkoThreshold_ - 1) {
    /* If we can't TKO the box, leave it one hit away */
    if (!incrementSoftTkoCount()) {
      return false;
    }
  }
  sumFailures_++;
  return sumFailures_ == tkoThreshold_;
}


bool TkoTracker::recordHardFailure() {
  std::lock_guard<std::mutex> lock(mx_);
  if (isHardTko()) {
    return false;
  }
  /* If we were not in soft TKO before, we just entered TKO. If we were
  in softTKO, we need to decrement the softTKO counter to reflect the state
  change */
  bool responsible = sumFailures_ < tkoThreshold_;
  if (!responsible) {
    decrementSoftTkoCount();
  }

  sumFailures_ = tkoThreshold_ + 1;
  return responsible;
}

void TkoTracker::recordSuccess() {
  if (sumFailures_ != 0) {
    std::lock_guard<std::mutex> lock(mx_);
    /* If we're coming out of softTKO, we need to decrement the counter */
    if (isSoftTko()) {
      decrementSoftTkoCount();
    }
    sumFailures_ = 0;
  }
}
}}} // facebook::memcache::mcrouter
