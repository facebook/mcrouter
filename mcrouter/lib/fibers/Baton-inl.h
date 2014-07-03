/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#include "mcrouter/lib/fibers/Fiber.h"
#include "mcrouter/lib/fibers/FiberManager.h"

namespace facebook { namespace memcache {

template <typename F>
void Baton::wait(F&& mainContextFunc) {
  auto& fm = FiberManager::getFiberManager();

  assert(fm.activeFiber_ != nullptr);

  auto& waitingFiber = waitingFiber_;
  auto f = [&mainContextFunc, &waitingFiber](Fiber& fiber) mutable {
    auto baton_fiber = waitingFiber.load();
    do {
      if (LIKELY(baton_fiber == WAITING_FIBER_EMPTY)) {
        continue;
      } else if (baton_fiber == WAITING_FIBER_POSTED ||
                 baton_fiber == WAITING_FIBER_TIMEOUT) {
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
  auto& baton = *this;
  auto timeoutFunc = [&baton]() mutable {
    baton.postHelper(WAITING_FIBER_TIMEOUT);
  };

  TimeoutHandle timeout_handle(std::ref(timeoutFunc));

  auto& fm = FiberManager::getFiberManager();
  fm.timeoutManager_.registerTimeout(timeout_handle, timeout);

  wait(std::move(mainContextFunc));

  timeout_handle.tryCancel();

  return waitingFiber_ == WAITING_FIBER_POSTED;
}

}}
