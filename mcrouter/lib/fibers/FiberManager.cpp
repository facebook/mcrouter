/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "FiberManager.h"

#include <sys/syscall.h>
#include <unistd.h>

#include <cassert>
#include <stdexcept>

#include "mcrouter/lib/fibers/Fiber.h"
#include "mcrouter/lib/fibers/LoopController.h"

namespace facebook { namespace memcache {

__thread FiberManager* FiberManager::currentFiberManager_ = nullptr;

FiberManager::FiberManager(std::unique_ptr<LoopController> loopController,
                           Options options) :
    loopController_(std::move(loopController)),
    options_(options),
    timeoutManager_(*loopController_) {
  TAILQ_INIT(&readyFibers_);
  TAILQ_INIT(&fibersPool_);

  loopController_->setFiberManager(this);
}

FiberManager::~FiberManager() {
  if (isLoopScheduled_) {
    loopController_->cancel();
  }

  Fiber* fiberIt;
  Fiber* fiberItNext;
  TAILQ_FOREACH_SAFE(fiberIt, &fibersPool_, entry_, fiberItNext) {
    delete fiberIt;
  }
  assert(TAILQ_EMPTY(&readyFibers_));
  assert(fibersActive_ == 0);
}

LoopController& FiberManager::loopController() {
  return *loopController_;
}

const LoopController& FiberManager::loopController() const {
  return *loopController_;
}

bool FiberManager::hasTasks() const {
  return fibersActive_ > 0 ||
         !remoteReadyQueue_.empty() ||
         !remoteTaskQueue_.empty();
}

Fiber* FiberManager::getFiber() {
  Fiber* fiber = nullptr;
  if (TAILQ_FIRST(&fibersPool_) == nullptr) {
    fiber = new Fiber(*this);
    ++fibersAllocated_;
  } else {
    fiber = TAILQ_FIRST(&fibersPool_);
    TAILQ_REMOVE(&fibersPool_, fiber, entry_);
    assert(fibersPoolSize_ > 0);
    --fibersPoolSize_;
  }
  ++fibersActive_;
  assert(fiber);
  return fiber;
}

void FiberManager::setExceptionCallback(FiberManager::ExceptionCallback ec) {
  exceptionCallback_ = std::move(ec);
}

size_t FiberManager::fibersAllocated() const {
  return fibersAllocated_;
}

size_t FiberManager::fibersPoolSize() const {
  return fibersPoolSize_;
}

size_t FiberManager::stackHighWatermark() const {
  return stackHighWatermark_;
}

void FiberManager::remoteReadyInsert(Fiber* fiber) {
  if (remoteReadyQueue_.insertHead(fiber)) {
    loopController_->scheduleThreadSafe();
  }
}

}}
