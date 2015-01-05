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

#include <functional>

namespace facebook { namespace memcache {

class FiberManager;

class LoopController {
 public:
  typedef std::chrono::steady_clock Clock;
  typedef std::chrono::time_point<Clock> TimePoint;

  virtual ~LoopController() {}

  /**
   * Called by FiberManager to associate itself with the LoopController.
   */
  virtual void setFiberManager(FiberManager*) = 0;

  /**
   * Called by FiberManager to schedule the loop function run
   * at some point in the future.
   */
  virtual void schedule() = 0;

  /**
   * Same as schedule(), but safe to call from any thread.
   */
  virtual void scheduleThreadSafe() = 0;

  /**
   * Called by FiberManager to cancel a previously scheduled
   * loop function run.
   */
  virtual void cancel() = 0;

  /**
   * Called by FiberManager to schedule some function to be run at some time.
   */
  virtual void timedSchedule(std::function<void()> func, TimePoint time) = 0;
};

}}  // facebook::memcache
