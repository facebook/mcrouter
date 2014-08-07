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

#include <mcrouter/lib/fibers/TimeoutController.h>

namespace facebook { namespace memcache {

class Fiber;

/**
 * @class Baton
 *
 * Primitive which allows to put current Fiber to sleep and wake it from another
 * Fiber/thread.
 */
class Baton {
 public:
  /**
   * Puts active fiber to sleep. Returns when post is called.
   */
  void wait();

  /**
   * Puts active fiber to sleep. Returns when post is called.
   *
   * @param mainContextFunc this function is immediately executed on the main
   *        context.
   */
  template <typename F>
  void wait(F&& mainContextFunc);

  /**
   * Puts active fiber to sleep. Returns when post is called.
   *
   * @param timeout Baton will be automatically awaken if timeout is hit
   *
   * @return true if was posted, false if timeout expired
   */
  bool timed_wait(TimeoutController::Duration timeout);

  /**
   * Puts active fiber to sleep. Returns when post is called.
   *
   * @param timeout Baton will be automatically awaken if timeout is hit
   * @param mainContextFunc this function is immediately executed on the main
   *        context.
   *
   * @return true if was posted, false if timeout expired
   */
  template <typename F>
  bool timed_wait(TimeoutController::Duration timeout, F&& mainContextFunc);

  /**
   * Checks if the baton has been posted without blocking.
   * @return    true iff the baton has been posted.
   */
  bool try_wait();

  /**
   * Wakes up Fiber which was waiting on this Baton (or if no Fiber is waiting,
   * next wait() call will return immediately).
   */
  void post();

 private:
  void postHelper(intptr_t new_value);

  static constexpr intptr_t WAITING_FIBER_EMPTY = 0;
  static constexpr intptr_t WAITING_FIBER_POSTED = -1;
  static constexpr intptr_t WAITING_FIBER_TIMEOUT = -2;

  std::atomic<intptr_t> waitingFiber_{WAITING_FIBER_EMPTY};
};

}}

#include "mcrouter/lib/fibers/Baton-inl.h"
