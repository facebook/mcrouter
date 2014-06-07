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
   * Wakes up Fiber which was waiting on this Baton (or if no Fiber is waiting,
   * next wait() call will return immediately).
   */
  void post();

 private:
  static constexpr intptr_t WAITING_FIBER_EMPTY = 0;
  static constexpr intptr_t WAITING_FIBER_POSTED = -1;

  std::atomic<intptr_t> waitingFiber_{WAITING_FIBER_EMPTY};
};

}}

#include "mcrouter/lib/fibers/Baton-inl.h"
