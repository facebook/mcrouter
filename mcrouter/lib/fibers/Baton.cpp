/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/lib/fibers/Baton.h"

namespace facebook { namespace memcache {

void Baton::wait() {
  wait([](){});
}

bool Baton::timed_wait(TimeoutController::Duration timeout) {
  return timed_wait(timeout, [](){});
}

void Baton::post() {
  postHelper(WAITING_FIBER_POSTED);
}

void Baton::postHelper(intptr_t new_value) {
  auto fiber = waitingFiber_.load();

  do {
    if (fiber == WAITING_FIBER_POSTED || fiber == WAITING_FIBER_TIMEOUT) {
      return;
    }
  } while (!waitingFiber_.compare_exchange_weak(fiber, new_value));

  if (fiber != WAITING_FIBER_EMPTY) {
    reinterpret_cast<Fiber*>(fiber)->setData(0);
  }
}

bool Baton::try_wait() {
  return waitingFiber_.load() == WAITING_FIBER_POSTED;
}

}}
