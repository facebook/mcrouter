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

void Baton::post() {
  auto fiber = waitingFiber_.load();
  intptr_t new_value;

  do {
    if (fiber == WAITING_FIBER_POSTED) {
      return;
    }
    if (fiber == WAITING_FIBER_EMPTY) {
      new_value = WAITING_FIBER_POSTED;
    } else {
      new_value = WAITING_FIBER_EMPTY;
    }
  } while (!waitingFiber_.compare_exchange_weak(fiber, new_value));

  if (new_value == WAITING_FIBER_EMPTY) {
    reinterpret_cast<Fiber*>(fiber)->setData(0);
  }
}

}}
