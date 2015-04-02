/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FiberManager.h"

#include <sys/syscall.h>
#include <unistd.h>

#include <cassert>
#include <stdexcept>

#include "mcrouter/lib/fibers/Fiber.h"
#include "mcrouter/lib/fibers/LoopController.h"
#include "mcrouter/lib/fbi/cpp/LogFailure.h"

namespace facebook { namespace memcache {

__thread FiberManager* FiberManager::currentFiberManager_ = nullptr;

FiberManager::FiberManager(std::unique_ptr<LoopController> loopController,
                           Options options) :
    loopController_(std::move(loopController)),
    options_(options),
    exceptionCallback_([](std::exception_ptr e, std::string context) {
        try {
          std::rethrow_exception(e);
        } catch (const std::exception& e) {
          failure::log("FiberManager", failure::Category::kOther,
                       "Exception {} with message '{}' was thrown in "
                       "FiberManager with context '{}'",
                       typeid(e).name(), e.what(), context);
          throw;
        } catch (...) {
          failure::log("FiberManager", failure::Category::kOther,
                       "Unknown exception was thrown in FiberManager with "
                       "context '{}'",
                       context);
          throw;
        }
      }),
    timeoutManager_(std::make_shared<TimeoutController>(*loopController_)) {
  loopController_->setFiberManager(this);
}

FiberManager::~FiberManager() {
  if (isLoopScheduled_) {
    loopController_->cancel();
  }

  Fiber* fiberIt;
  Fiber* fiberItNext;
  while (!fibersPool_.empty()) {
    fibersPool_.pop_front_and_dispose([] (Fiber* fiber) {
      delete fiber;
    });
  }
  assert(readyFibers_.empty());
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
  if (fibersPool_.empty()) {
    fiber = new Fiber(*this);
    ++fibersAllocated_;
  } else {
    fiber = &fibersPool_.front();
    fibersPool_.pop_front();
    assert(fibersPoolSize_ > 0);
    --fibersPoolSize_;
  }
  ++fibersActive_;
  assert(fiber);
  return fiber;
}

void FiberManager::setExceptionCallback(FiberManager::ExceptionCallback ec) {
  assert(ec);
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
