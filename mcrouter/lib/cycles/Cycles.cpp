/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Cycles.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>
#include <vector>

#include <glog/logging.h>

#include "mcrouter/lib/cycles/Accumulator.h"
#include "mcrouter/lib/cycles/ExtractorThread.h"

using namespace facebook::memcache::cycles::detail;

namespace facebook { namespace memcache { namespace cycles {

namespace {

// Extractor thread.
ExtractorThread extractor;

// Clock
CyclesClock clock;

class IntervalContext {
 public:
  bool started() const {
    return startedAt > 0;
  }
  void reset() {
    startedAt = 0;
    labeled = false;
    valid = true;
  }
  bool setLabel(detail::IntervalLabel lbl) {
    if (labeled) {
      if (label == lbl) {
        return true;
      }
      valid = false;
      return false;
    }

    label = std::move(lbl);
    labeled = true;
    return true;
  }

 private:
  uint64_t startedAt{0};
  detail::IntervalLabel label;
  bool labeled{false};
  bool valid{true};

  friend bool facebook::memcache::cycles::start() noexcept;
  friend void facebook::memcache::cycles::finish() noexcept;
};
thread_local IntervalContext intervalCtx;

} // anonymous namespace

void attachEventBase(folly::EventBase& eventBase) {
  currentAccumulator().attachEventBase(eventBase);
}

IntervalGuard::IntervalGuard() {
  valid_ = start();
}

IntervalGuard::~IntervalGuard() {
  if (valid_) {
    finish();
  }
}

bool start() noexcept {
  if (!extractor.running() || intervalCtx.started()) {
    return false;
  }

  intervalCtx.startedAt = clock.ticks();
  return true;
}

void finish() noexcept {
  if (!extractor.running()) {
    return;
  }

  if (intervalCtx.started() && intervalCtx.valid && intervalCtx.labeled) {
    uint64_t length = clock.ticks() - intervalCtx.startedAt;
    currentAccumulator().add(Interval(length, std::move(intervalCtx.label)));
  }
  intervalCtx.reset();
}

bool label(uint64_t requestType, uint64_t requestId) {
  if (!intervalCtx.started()) {
    return false;
  }

  return intervalCtx.setLabel(IntervalLabel(requestType, requestId));
}

void startExtracting(std::function<void(CycleStats)> func) {
  extractor.start(std::move(func));
}

void stopExtracting() {
  extractor.stop();
}

}}} // namespace facebook::memcache::cycles
