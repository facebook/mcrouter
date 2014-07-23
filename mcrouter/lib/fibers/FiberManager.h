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
#include <memory>
#include <queue>
#include <unordered_set>
#include <vector>

#include "folly/Likely.h"
#include "folly/wangle/Try.h"
#include "mcrouter/lib/fbi/cpp/AtomicLinkedList.h"
#include "mcrouter/lib/fbi/cpp/traits.h"
#include "mcrouter/lib/fbi/queue.h"
#include "mcrouter/lib/fibers/Fiber.h"
#include "mcrouter/lib/fibers/TimeoutController.h"

#ifdef USE_GUARD_ALLOCATOR
#include "mcrouter/lib/fibers/GuardPageAllocator.h"
#endif

namespace facebook { namespace memcache {

class Baton;
class Fiber;
class LoopController;

/**
 * @class FiberManager
 * @brief Single-threaded task execution engine.
 *
 * FiberManager allows semi-parallel task execution on the same thread. Each
 * task can notify FiberManager that it is blocked on something (via await())
 * call. This will pause execution of this task and it will be resumed only
 * when it is unblocked (via setData()).
 */
class FiberManager {
 public:
  struct Options {
    /**
     * Maximum stack size for fibers which will be used for executing all the
     * tasks.
     */
#ifdef __SANITIZE_ADDRESS__
    /* ASAN needs a lot of extra stack space.
       16x is a conservative estimate, 8x also worked with tests
       where it mattered.  Note that overallocating here does not necessarily
       increase RSS, since unused memory is pretty much free. */
    size_t stackSize{16 * 16 * 1024};
#else
    size_t stackSize{16 * 1024};
#endif

    /**
     * Record exact amount of stack used.
     *
     * This is fairly expensive: we fill each newly allocated stack
     * with some known value and find the boundary of unused stack
     * with linear search every time we surrender the stack back to fibersPool.
     */
    bool debugRecordStackUsed{false};

    /**
     * Keep at most this many free fibers in the pool.
     * This way the total number of fibers in the system is always bounded
     * by the number of active fibers + maxFibersPoolSize.
     */
    size_t maxFibersPoolSize{1000};

    constexpr Options() {}
  };

  typedef std::function<void(std::exception_ptr)> ExceptionCallback;

  /**
   * Initializes, but doesn't start FiberManager loop
   *
   * @param options FiberManager options
   */
  explicit FiberManager(std::unique_ptr<LoopController> loopController,
                        Options options = Options());

  ~FiberManager();

  /**
   * Controller access.
   */
  LoopController& loopController();
  const LoopController& loopController() const;

  /**
   * Keeps running ready tasks until the list of ready tasks is empty.
   *
   * @return True if there are any waiting tasks remaining.
   */
  bool loopUntilNoReady();

  /**
   * @return true if there are outstanding tasks.
   */
  bool hasTasks() const;

  /**
   * Sets exception callback which will be called if any of the tasks throws an
   * exception.
   *
   * @param ec
   */
  void setExceptionCallback(ExceptionCallback ec);

  /**
   * Add a new task to be executed. Must be called from FiberManager's thread.
   *
   * @param func Task functor; must have a signature of `void func()`.
   *             The object will be destroyed once task execution is complete.
   */
  template <typename F>
  void addTask(F&& func);

  /**
   * Add a new task to be executed. Safe to call from other threads.
   *
   * @param func Task function; must have a signature of `void func()`.
   *             The object will be destroyed once task execution is complete.
   */
  template <typename F>
  void addTaskRemote(F&& func);

  /**
   * Add a new task. When the task is complete, execute finally(Try<Result>&&)
   * on the main context.
   *
   * @param func Task functor; must have a signature of `T func()` for some T.
   * @param finally Finally functor; must have a signature of
   *                `void finally(Try<T>&&)` and will be passed
   *                the result of func() (including the exception if occurred).
   */
  template <typename F, typename G>
  void addTaskFinally(F&& func, G&& finally);

  /**
   * Lower-level version of addTaskFinally.
   *
   * Both func() and finally() will be passed a pointer to a stack-allocated
   * storage of resultSize bytes, as well as the context.
   *
   * @param cleanup  If non-nullptr, will be called exactly once after finally()
   *                 with context as the parameter.  Must not throw.
   *                 (Useful if you want to allocate a context on the heap).
   */
  void addTaskFinally(size_t resultSize,
                      void (*func)(intptr_t resultLoc, intptr_t context),
                      void (*finally)(intptr_t resultLoc, intptr_t context),
                      intptr_t context,
                      void (*cleanup)(intptr_t context));

  /**
   * Blocks task execution until given promise is fulfilled.
   *
   * Calls function passing in a FiberPromise<T>, which has to be fulfilled.
   *
   * @return data which was used to fulfill the promise.
   */
  template <typename F>
  typename FirstArgOf<F>::type::value_type
  await(F&& func);

  /**
   * If called from a fiber, immediately switches to the FiberManager's context
   * and runs func(), going back to the Fiber's context after completion.
   * Outside a fiber, just calls func() directly.
   *
   * @return value returned by func().
   */
  template <typename F>
  typename std::result_of<F()>::type
  runInMainContext(F&& func);

  /**
   * @return How many fiber objects (and stacks) has this manager allocated.
   */
  size_t fibersAllocated() const;

  /**
   * @return How many of the allocated fiber objects are currently
   * in the free pool.
   */
  size_t fibersPoolSize() const;

  /**
   * @return What was the most observed fiber stack usage (in bytes).
   */
  size_t stackHighWatermark() const;

  static FiberManager& getFiberManager();
  static FiberManager* getFiberManagerUnsafe();

 private:
  friend class Baton;
  friend class Fiber;

  struct RemoteTask {
    template <typename F>
    explicit RemoteTask(F&& f) : func(std::move(f)) {}
    std::function<void()> func;
    AtomicLinkedListHook<RemoteTask> nextRemoteTask;
  };

  TAILQ_HEAD(FiberTailQHead, Fiber);

  Fiber* activeFiber_{nullptr}; /**< active fiber, nullptr on main context */

  FiberTailQHead readyFibers_;  /**< queue of fibers ready to be executed */
  FiberTailQHead fibersPool_;   /**< pool of unitialized Fiber objects */

  size_t fibersAllocated_{0};   /**< total number of fibers allocated */
  size_t fibersPoolSize_{0};    /**< total number of fibers in the free pool */
  size_t fibersActive_{0};      /**< number of running or blocked fibers */

  Fiber::Context mainContext_;  /**< stores loop function context */

  std::unique_ptr<LoopController> loopController_;
  bool isLoopScheduled_{false}; /**< was the ready loop scheduled to run? */

  /**
   * When we are inside FiberManager loop this points to FiberManager. Otherwise
   * it's nullptr
   */
  static __thread FiberManager* currentFiberManager_;

  /**
   * runInMainContext implementation for non-void functions.
   */
  template <typename F>
  typename std::enable_if<
    !std::is_same<typename std::result_of<F()>::type, void>::value,
    typename std::result_of<F()>::type>::type
  runInMainContextHelper(F&& func);

  /**
   * runInMainContext implementation for void functions
   */
  template <typename F>
  typename std::enable_if<
    std::is_same<typename std::result_of<F()>::type, void>::value,
    void>::type
  runInMainContextHelper(F&& func);

  /**
   * Allocator used to allocate stack for Fibers in the pool.
   * Allocates stack on the stack of the main context.
   */
#ifdef USE_GUARD_ALLOCATOR
  /* This is too slow for production use; can be fixed
     if we allocated all stack storage once upfront */
  GuardPageAllocator stackAllocator_;
#else
  std::allocator<unsigned char> stackAllocator_;
#endif

  const Options options_;       /**< FiberManager options */

  /**
   * Largest observed individual Fiber stack usage in bytes.
   */
  size_t stackHighWatermark_{0};

  /**
   * Schedules a loop with loopController (unless already scheduled before).
   */
  void ensureLoopScheduled();

  /**
   * @return An initialized Fiber object from the pool
   */
  Fiber* getFiber();

  /**
   * Function passed to the await call.
   */
  std::function<void(Fiber&)> awaitFunc_;

  /**
   * Function passed to the runInMainContext call.
   */
  std::function<void()> immediateFunc_;

  ExceptionCallback exceptionCallback_; /**< task exception callback */

  AtomicLinkedList<Fiber, &Fiber::nextRemoteReady_> remoteReadyQueue_;

  AtomicLinkedList<RemoteTask, &RemoteTask::nextRemoteTask> remoteTaskQueue_;

  TimeoutController timeoutManager_;

  void runReadyFiber(Fiber* fiber);
  void remoteReadyInsert(Fiber* fiber);
};

namespace fiber {

/**
 * Add a new task to be executed.
 *
 * @param func Task functor; must have a signature of `void func()`.
 *             The object will be destroyed once task execution is complete.
 */
template <typename F>
inline void addTask(F&& func) {
  return FiberManager::getFiberManager().addTask(std::forward<F>(func));
}

/**
 * Add a new task. When the task is complete, execute finally(Try<Result>&&)
 * on the main context.
 *
 * @param func Task functor; must have a signature of `T func()` for some T.
 * @param finally Finally functor; must have a signature of
 *                `void finally(Try<T>&&)` and will be passed
 *                the result of func() (including the exception if occurred).
 */
template <typename F, typename G>
inline void addTaskFinally(F&& func, G&& finally) {
  return FiberManager::getFiberManager().addTaskFinally(
    std::forward<F>(func), std::forward<G>(finally));
}

/**
 * Blocks task execution until given promise is fulfilled.
 *
 * Calls function passing in a FiberPromise<T>, which has to be fulfilled.
 *
 * @return data which was used to fulfill the promise.
 */
template <typename F>
typename FirstArgOf<F>::type::value_type
inline await(F&& func) {
  return FiberManager::getFiberManager().await(std::forward<F>(func));
}

/**
 * If called from a fiber, immediately switches to the FiberManager's context
 * and runs func(), going back to the Fiber's context after completion.
 * Outside a fiber, just calls func() directly.
 *
 * @return value returned by func().
 */
template <typename F>
typename std::result_of<F()>::type
inline runInMainContext(F&& func) {
  auto fm = FiberManager::getFiberManagerUnsafe();
  if (UNLIKELY(fm == nullptr)) {
    return func();
  }
  return fm->runInMainContext(std::forward<F>(func));
}

}

}}

#include "FiberManager-inl.h"
