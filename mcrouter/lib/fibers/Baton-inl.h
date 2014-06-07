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
    intptr_t new_value;
    do {
      if (baton_fiber == WAITING_FIBER_EMPTY) {
        new_value = reinterpret_cast<intptr_t>(&fiber);
      } else if (LIKELY(baton_fiber == WAITING_FIBER_POSTED)) {
        new_value = WAITING_FIBER_EMPTY;
      } else {
        throw std::logic_error("Some Fiber is already waiting on this Baton.");
      }
    } while(!waitingFiber.compare_exchange_weak(baton_fiber, new_value));

    if (new_value == WAITING_FIBER_EMPTY) {
      fiber.setData(0);
    }

    mainContextFunc();
  };

  fm.awaitFunc_ = std::ref(f);
  fm.activeFiber_->preempt(Fiber::AWAITING);
}

}}
