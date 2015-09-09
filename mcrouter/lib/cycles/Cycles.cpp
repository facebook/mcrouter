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

#include <folly/Memory.h>
#include "mcrouter/lib/cycles/Accumulator.h"
#include "mcrouter/lib/cycles/ExtractorThread.h"

using namespace facebook::memcache::cycles::detail;

namespace facebook { namespace memcache { namespace cycles {

namespace {

// Extractor thread.
ExtractorThread extractor;

// Clock
std::unique_ptr<Clock> clock = folly::make_unique<CyclesClock>();

class IntervalContext {
 public:
  bool started() const {
    return metering_.ticks > 0;
  }
  void reset() {
    metering_ = Metering{0, 0};
    labeled_ = false;
    valid_ = true;
  }
  bool setLabel(detail::IntervalLabel lbl) {
    if (labeled_) {
      if (label_ == lbl) {
        return true;
      }
      valid_ = false;
      return false;
    }

    label_ = std::move(lbl);
    labeled_ = true;
    return true;
  }

 private:
  Metering metering_{0, 0};
  detail::IntervalLabel label_;
  bool labeled_{false};
  bool valid_{true};

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

  intervalCtx.metering_ = clock->read();
  return true;
}

void finish() noexcept {
  if (!extractor.running()) {
    return;
  }

  if (intervalCtx.started() && intervalCtx.valid_ && intervalCtx.labeled_) {
    auto metering = clock->read() - intervalCtx.metering_;
    currentAccumulator().add(Interval(metering, std::move(intervalCtx.label_)));
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

void stopExtracting() noexcept {
  extractor.stop();
}

void setClock(std::unique_ptr<Clock> clk) {
  clock = std::move(clk);
}

}}} // namespace facebook::memcache::cycles
