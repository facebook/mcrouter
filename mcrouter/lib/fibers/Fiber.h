/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <functional>

#include <boost/context/all.hpp>
#include <boost/version.hpp>

#include "mcrouter/lib/fbi/cpp/AtomicLinkedList.h"
#include "mcrouter/lib/fbi/queue.h"

namespace facebook { namespace memcache {

class Baton;
class FiberManager;

/**
 * @class Fiber
 * @brief Fiber object used by FiberManager to execute tasks.
 *
 * Each Fiber object can be executing at most one task at a time. In active
 * phase it is running the task function and keeps its context.
 * Fiber is also used to pass data to blocked task and thus unblock it.
 * Each Fiber may be associated with a single FiberManager.
 */
class Fiber {
 public:
#if BOOST_VERSION >= 105200
  typedef boost::context::fcontext_t Context;
#else
  typedef boost::ctx::fcontext_t Context;
#endif
  /**
   * Sets data for the blocked task
   *
   * @param data this data will be returned by await() when task is resumed.
   */
  void setData(intptr_t data);

  Fiber(const Fiber&) = delete;
  Fiber& operator=(const Fiber&) = delete;

  ~Fiber();
 private:
  enum State {
    INVALID,                    /**< Does't have task function */
    NOT_STARTED,                /**< Has task function, not started */
    READY_TO_RUN,               /**< Was started, blocked, then unblocked */
    RUNNING,                    /**< Is running right now */
    AWAITING,                   /**< Is currently blocked */
    AWAITING_IMMEDIATE,         /**< Was preempted to run an immediate function,
                                     and will be resumed right away */
  };

  State state_{INVALID};        /**< current Fiber state */

  friend class Baton;
  friend class FiberManager;

  explicit Fiber(FiberManager& fiberManager);

  template <typename F>
  void setFunction(F&& func);

  template <typename F, typename G>
  void setFunctionFinally(size_t resultSize, F&& func, G&& finally);

  static void fiberFuncHelper(intptr_t fiber);
  void fiberFunc();

  /**
   * Switch out of fiber context into the main context,
   * performing necessary housekeeping for the new state.
   *
   * @param state New state, must not be RUNNING.
   *
   * @return The value passed back from the main context.
   */
  intptr_t preempt(State state);

  /**
   * Examines how much of the stack we used at this moment and
   * registers with the FiberManager (for monitoring).
   */
  void recordStackPosition();

  FiberManager& fiberManager_;  /**< Associated FiberManager */
  Context* contextPtr_;         /**< current task execution context */
#if BOOST_VERSION < 105200
  Context contextImpl_;
#endif
  intptr_t data_;               /**< Used to keep some data with the Fiber */
  std::function<void()> func_;  /**< task function */

  /**
   * Points to next fiber in remote ready list
   */
  AtomicLinkedListHook<Fiber> nextRemoteReady_;

  /**
   * addTaskFinally implementation.
   * resultSize_ bytes are allocated on the stack (intptr_t-aligned);
   * the pointer to that location is passed to both resultFunc_(),
   * which is called on Fiber context and finallyFunc_ which is
   * called in main context (while Fiber's stack contents are still valid).
   */
  size_t resultSize_{0};
  std::function<void(intptr_t)> resultFunc_;
  std::function<void(intptr_t)> finallyFunc_;

  TAILQ_ENTRY(Fiber) entry_;    /**< entry for different FiberManager queues */

  pid_t threadId_{0};
};

}}

#include <mcrouter/lib/fibers/Fiber-inl.h>
