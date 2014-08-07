/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#include "mcrouter/lib/fibers/GenericBaton.h"
#include "mcrouter/lib/fibers/FiberManager.h"

#include <chrono>

namespace facebook { namespace memcache {

void GenericBaton::wait() {
  if (fiber::onFiber()) {
    fiberBaton_.wait();
  } else {
    threadBaton_.wait();
  }
}

bool GenericBaton::try_wait() {
  if (fiber::onFiber()) {
    return fiberBaton_.try_wait();
  } else {
    return threadBaton_.try_wait();
  }
}

bool GenericBaton::timed_wait(TimeoutController::Duration timeout) {
  if (fiber::onFiber()) {
    return fiberBaton_.timed_wait(timeout);
  } else {
    return threadBaton_.timed_wait(std::chrono::system_clock::now() + timeout);
  }
}

void GenericBaton::post() {
  // We need to post both batons since we aren't sure if
  // the waiter is a fiber or a thread
  fiberBaton_.post();
  threadBaton_.post();
}

}}
