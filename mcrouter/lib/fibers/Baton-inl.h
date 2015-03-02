/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/fibers/Fiber.h"
#include "mcrouter/lib/fibers/FiberManager.h"

namespace facebook { namespace memcache {

inline Baton::Baton() : Baton(NO_WAITER) {
  assert(Baton(NO_WAITER).futex_.futex == static_cast<uint32_t>(NO_WAITER));
  assert(Baton(POSTED).futex_.futex == static_cast<uint32_t>(POSTED));
  assert(Baton(TIMEOUT).futex_.futex == static_cast<uint32_t>(TIMEOUT));
  assert(Baton(THREAD_WAITING).futex_.futex ==
         static_cast<uint32_t>(THREAD_WAITING));

  assert(futex_.futex.is_lock_free());
  assert(waitingFiber_.is_lock_free());
}

template <typename F>
void Baton::wait(F&& mainContextFunc) {
  auto fm = FiberManager::getFiberManagerUnsafe();
  if (!fm || !fm->activeFiber_) {
    mainContextFunc();
    return waitThread();
  }

  return waitFiber(*fm, std::forward<F>(mainContextFunc));
}

template <typename F>
void Baton::waitFiber(FiberManager& fm, F&& mainContextFunc) {
  auto& waitingFiber = waitingFiber_;
  auto f = [&mainContextFunc, &waitingFiber](Fiber& fiber) mutable {
    auto baton_fiber = waitingFiber.load();
    do {
      if (LIKELY(baton_fiber == NO_WAITER)) {
        continue;
      } else if (baton_fiber == POSTED || baton_fiber == TIMEOUT) {
        fiber.setData(0);
        break;
      } else {
        throw std::logic_error("Some Fiber is already waiting on this Baton.");
      }
    } while(!waitingFiber.compare_exchange_weak(
              baton_fiber,
              reinterpret_cast<intptr_t>(&fiber)));

    mainContextFunc();
  };

  fm.awaitFunc_ = std::ref(f);
  fm.activeFiber_->preempt(Fiber::AWAITING);
}

template <typename F>
bool Baton::timed_wait(TimeoutController::Duration timeout,
                       F&& mainContextFunc) {
  auto fm = FiberManager::getFiberManagerUnsafe();

  if (!fm || !fm->activeFiber_) {
    mainContextFunc();
    return timedWaitThread(timeout);
  }

  auto& baton = *this;
  auto timeoutFunc = [&baton]() mutable {
    baton.postHelper(TIMEOUT);
  };

  auto id = fm->timeoutManager_->registerTimeout(
    std::ref(timeoutFunc), timeout);

  waitFiber(*fm, std::move(mainContextFunc));

  auto posted = waitingFiber_ == POSTED;

  if (posted) {
    fm->timeoutManager_->cancel(id);
  }

  return posted;
}

template<typename C, typename D>
bool Baton::timed_wait(const std::chrono::time_point<C,D>& timeout) {
  auto now = C::now();

  if (LIKELY(now <= timeout)) {
    return timed_wait(
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout - now));
  } else {
    return timed_wait(TimeoutController::Duration(0));
  }
}


}}
