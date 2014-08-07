/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <folly/Baton.h>

#include "mcrouter/lib/fibers/Baton.h"

namespace facebook { namespace memcache {

/**
 * @class GenericBaton
 *
 * Primitive that puts the current fiber/thread to sleep until woken up by
 * another thread/fiber. This is different from Baton (see Baton.h) in that
 * the waiter can either be a thread or fiber. This is build on top of
 * mcrouter/lib/fibers/Baton.h and folly/Baton.h
 */
class GenericBaton {
 public:
  /**
   * Puts active fiber / thread to sleep. Returns when post is called.
   */
  void wait();

  /**
   * Check if the baton has been posted without blocking.
   * @return           true if posted, false otherwise
   */
  bool try_wait();

  /**
   * Puts active fiber / thread to sleep. Returns when post is called.
   *
   * @param timeout    The absolute time until which the fiber / thread can
   *                   block
   * @return           true if was posted, false if timeout expired
   */
  template<typename C, typename D = typename C::duration>
  bool timed_wait(const std::chrono::time_point<C,D>& timeout);

  /**
   * Wakes up Fiber/ thread  which was waiting on this Baton. If no fiber or
   * thread was waiting, the baton remembers that it was posted to, so that
   * a subsequent wait call returns immediately
   */
  void post();

 private:
  Baton fiberBaton_;
  folly::Baton<> threadBaton_;
};

}}

#include "mcrouter/lib/fibers/GenericBaton-inl.h"
