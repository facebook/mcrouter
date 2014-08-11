/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/lib/fibers/Baton.h"

#include <folly/detail/MemoryIdler.h>

namespace facebook { namespace memcache {

void Baton::wait() {
  wait([](){});
}

bool Baton::timed_wait(TimeoutController::Duration timeout) {
  return timed_wait(timeout, [](){});
}

void Baton::waitThread() {
  if (spinWaitForEarlyPost()) {
    assert(waitingFiber_.load(std::memory_order_acquire) == POSTED);
    return;
  }

  auto fiber = waitingFiber_.load();

  if (LIKELY(fiber == NO_WAITER &&
             waitingFiber_.compare_exchange_strong(fiber, THREAD_WAITING))) {
    do {
      folly::detail::MemoryIdler::futexWait(futex_.futex, THREAD_WAITING);
      fiber = waitingFiber_.load(std::memory_order_relaxed);
    } while (fiber == THREAD_WAITING);
  }

  if (LIKELY(fiber == POSTED)) {
    return;
  }

  // Handle errors
  if (fiber == TIMEOUT) {
    throw std::logic_error("Thread baton can't have timeout status");
  }
  if (fiber == THREAD_WAITING) {
    throw std::logic_error("Other thread is already waiting on this baton");
  }
  throw std::logic_error("Other fiber is already waiting on this baton");
}

bool Baton::spinWaitForEarlyPost() {
  static_assert(PreBlockAttempts > 0,
      "isn't this assert clearer than an uninitialized variable warning?");
  for (int i = 0; i < PreBlockAttempts; ++i) {
    if (try_wait()) {
      // hooray!
      return true;
    }
#if FOLLY_X64
    // The pause instruction is the polite way to spin, but it doesn't
    // actually affect correctness to omit it if we don't have it.
    // Pausing donates the full capabilities of the current core to
    // its other hyperthreads for a dozen cycles or so
    asm volatile ("pause");
#endif
  }

  return false;
}

bool Baton::timedWaitThread(TimeoutController::Duration timeout) {
  if (spinWaitForEarlyPost()) {
    assert(waitingFiber_.load(std::memory_order_acquire) == POSTED);
    return true;
  }

  auto fiber = waitingFiber_.load();

  if (LIKELY(fiber == NO_WAITER &&
             waitingFiber_.compare_exchange_strong(fiber, THREAD_WAITING))) {
    auto deadline = TimeoutController::Clock::now() + timeout;
    do {
      const auto wait_rv =
        futex_.futex.futexWaitUntil(THREAD_WAITING, deadline);
      if (wait_rv == folly::detail::FutexResult::TIMEDOUT) {
        return false;
      }
      fiber = waitingFiber_.load(std::memory_order_relaxed);
    } while (fiber == THREAD_WAITING);
  }

  if (LIKELY(fiber == POSTED)) {
    return true;
  }

  // Handle errors
  if (fiber == TIMEOUT) {
    throw std::logic_error("Thread baton can't have timeout status");
  }
  if (fiber == THREAD_WAITING) {
    throw std::logic_error("Other thread is already waiting on this baton");
  }
  throw std::logic_error("Other fiber is already waiting on this baton");
}

void Baton::post() {
  postHelper(POSTED);
}

void Baton::postHelper(intptr_t new_value) {
  auto fiber = waitingFiber_.load();

  do {
    if (fiber == THREAD_WAITING) {
      assert(new_value == POSTED);

      return postThread();
    }

    if (fiber == POSTED || fiber == TIMEOUT) {
      return;
    }
  } while (!waitingFiber_.compare_exchange_weak(fiber, new_value));

  if (fiber != NO_WAITER) {
    reinterpret_cast<Fiber*>(fiber)->setData(0);
  }
}

bool Baton::try_wait() {
  auto state = waitingFiber_.load();
  return state == POSTED;
}

void Baton::postThread() {
  auto expected = THREAD_WAITING;

  if (!waitingFiber_.compare_exchange_strong(expected, POSTED)) {
    return;
  }

  futex_.futex.futexWake(1);
}

}}
