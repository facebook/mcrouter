/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CyclesObserver.h"

#include "mcrouter/lib/cycles/Cycles.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/ProxyRequestContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

void CyclesObserver::starting(uintptr_t id) noexcept {
  if (!cycles::start()) {
    // Should never happen
    DCHECK(false) << "There is already one cycles interval "
                     "active in this thread";
  }
}

void CyclesObserver::runnable(uintptr_t id) noexcept {
}

void CyclesObserver::stopped(uintptr_t id) noexcept {
  if (auto sharedCtx = fiber_local::getSharedCtx()) {
    cycles::label(sharedCtx->origReq()->op, sharedCtx->requestId());
  }
  cycles::finish();
}

}}} // facebook::memcache::mcrouter
