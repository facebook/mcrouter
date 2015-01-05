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

#include "mcrouter/lib/fibers/LoopController.h"

namespace folly {
class EventBase;
}

namespace facebook { namespace memcache {

class FiberManager;

namespace mcrouter {

class EventBaseLoopController : public LoopController {
 public:
  explicit EventBaseLoopController();
  ~EventBaseLoopController();

  /**
   * Attach EventBase after LoopController was created.
   */
  void attachEventBase(folly::EventBase& eventBase);

  folly::EventBase* getEventBase() {
    return eventBase_;
  }

 private:
  class ControllerCallback;

  bool awaitingScheduling_{false};
  folly::EventBase* eventBase_{nullptr};
  std::unique_ptr<ControllerCallback> callback_;
  FiberManager* fm_{nullptr};
  std::atomic<bool> eventBaseAttached_{false};

  /* LoopController interface */

  void setFiberManager(FiberManager* fm) override;
  void schedule() override;
  void cancel() override;
  void runLoop();
  void scheduleThreadSafe() override;
  void timedSchedule(std::function<void()> func, TimePoint time) override;

  friend class FiberManager;
};

}}}  // facebook::memcache::mcrouter

#include "EventBaseLoopController-inl.h"
