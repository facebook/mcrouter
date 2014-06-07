/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

namespace facebook { namespace memcache {

class LoopController {
 public:
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
};

}}  // facebook::memcache
