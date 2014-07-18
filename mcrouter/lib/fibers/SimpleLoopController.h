/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "folly/Likely.h"
#include "mcrouter/lib/fibers/LoopController.h"

namespace facebook { namespace memcache {

class FiberManager;

class SimpleLoopController : public LoopController {
 public:
  SimpleLoopController()
      : fm_(nullptr),
        stopRequested_(false) {
  }

  /**
   * Run FiberManager loop; if no ready task are present,
   * run provided function. Stops after both stop() has been called
   * and no waiting tasks remain.
   */
  template <typename F>
  void loop(F&& func) {
    bool waiting = false;
    stopRequested_ = false;

    while (LIKELY(waiting || !stopRequested_)) {
      func();
      if (scheduled_) {
        scheduled_ = false;
        waiting = fm_->loopUntilNoReady();
      }
    }
  }

  /**
   * Requests exit from loop() as soon as all waiting tasks complete.
   */
  void stop() {
    stopRequested_ = true;
  }

  int remoteScheduleCalled() const {
    return remoteScheduleCalled_;
  }

  void schedule() override {
    scheduled_ = true;
  }

 private:
  FiberManager* fm_;
  std::atomic<bool> scheduled_{false};
  bool stopRequested_;
  std::atomic<int> remoteScheduleCalled_{0};

  /* LoopController interface */

  void setFiberManager(FiberManager* fm) override {
    fm_ = fm;
  }

  void cancel() override {
    scheduled_ = false;
  }

  void scheduleThreadSafe() override {
    ++remoteScheduleCalled_;
    scheduled_ = true;
  }

  friend class FiberManager;
};

}}  // facebook::memcache
