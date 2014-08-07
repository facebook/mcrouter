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

template<typename C, typename D>
bool GenericBaton::timed_wait(const std::chrono::time_point<C,D>& timeout) {
  if (fiber::onFiber()) {
    // TODO(mssarang): Extend memcache::Baton::timed_wait to take absolute time
    auto now = C::now();
    if (now >= timeout) {
      return fiberBaton_.timed_wait(TimeoutController::Duration(0));
    } else {
      return fiberBaton_.timed_wait(timeout - now);
    }
  } else {
    return threadBaton_.timed_wait(timeout);
  }
}

}}
