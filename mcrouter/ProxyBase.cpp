/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyBase.h"

#include <folly/fibers/EventBaseLoopController.h>
#include <folly/fibers/FiberManager.h>
#include <folly/Random.h>

#include "mcrouter/config-impl.h"
#include "mcrouter/config.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/McrouterInstanceBase.h"
#include "mcrouter/options.h"
#include "mcrouter/ProxyDestinationMap.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace {

folly::fibers::FiberManager::Options getFiberManagerOptions(
    const McrouterOptions& opts) {
  folly::fibers::FiberManager::Options fmOpts;
  fmOpts.stackSize = opts.fibers_stack_size;
  fmOpts.recordStackEvery = opts.fibers_record_stack_size_every;
  fmOpts.maxFibersPoolSize = opts.fibers_max_pool_size;
  fmOpts.useGuardPages = opts.fibers_use_guard_pages;
  fmOpts.fibersPoolResizePeriodMs = opts.fibers_pool_resize_period_ms;
  return fmOpts;
}

} // anonymous

ProxyBase::ProxyBase(
    McrouterInstanceBase& rtr,
    size_t id,
    folly::EventBase& evb)
    : router_(rtr),
      id_(id),
      eventBase_(evb),
      fiberManager_(
          fiber_local::ContextTypeTag(),
          folly::make_unique<folly::fibers::EventBaseLoopController>(),
          getFiberManagerOptions(router_.opts())),
      asyncLog_(router_.opts()),
      destinationMap_(folly::make_unique<ProxyDestinationMap>(this)) {
  // Setup a full random seed sequence
  folly::Random::seed(randomGenerator_);

  statsContainer_ = folly::make_unique<ProxyStatsContainer>(*this);
}

const McrouterOptions& ProxyBase::getRouterOptions() const {
  return router_.opts();
}

uint64_t ProxyBase::nextRequestId() {
  return ++nextReqId_;
}

} // mcrouter
} // memcache
} // facebook
