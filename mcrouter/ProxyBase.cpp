/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyBase.h"

#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/options.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

const McrouterOptions& ProxyBase::getRouterOptions() const {
  return router_.opts();
}

folly::fibers::FiberManager::Options ProxyBase::getFiberManagerOptions(
    const McrouterOptions& opts) {
  folly::fibers::FiberManager::Options fmOpts;
  fmOpts.stackSize = opts.fibers_stack_size;
  fmOpts.recordStackEvery = opts.fibers_record_stack_size_every;
  fmOpts.maxFibersPoolSize = opts.fibers_max_pool_size;
  fmOpts.useGuardPages = opts.fibers_use_guard_pages;
  fmOpts.fibersPoolResizePeriodMs = opts.fibers_pool_resize_period_ms;
  return fmOpts;
}

void ProxyBase::FlushCallback::runLoopCallback() noexcept {
  // Always reschedlue until the end of event loop.
  if (!rescheduled_) {
    rescheduled_ = true;
    proxy_.eventBase().getEventBase().runInLoop(this, true /* thisIteration */);
    return;
  }
  rescheduled_ = false;

  auto cbs = std::move(flushList_);
  while (!cbs.empty()) {
    folly::EventBase::LoopCallback* callback = &cbs.front();
    cbs.pop_front();
    callback->runLoopCallback();
  }
}

} // mcrouter
} // memcache
} // facebook
