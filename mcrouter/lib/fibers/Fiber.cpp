/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "Fiber.h"

#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

#include <folly/Likely.h>
#include <folly/Portability.h>

#include "mcrouter/lib/fibers/BoostContextCompatibility.h"
#include "mcrouter/lib/fibers/FiberManager.h"

namespace facebook { namespace memcache {

namespace {
static const uint64_t kMagic8Bytes = 0xfaceb00cfaceb00c;

pid_t localThreadId() {
  static __thread pid_t threadId = syscall(SYS_gettid);
  return threadId;
}

static void fillMagic(const FContext& context) {
  uint64_t* begin = static_cast<uint64_t*>(context.stackLimit());
  uint64_t* end = static_cast<uint64_t*>(context.stackBase());

  std::fill(begin, end, kMagic8Bytes);
}

/* Size of the region from p + nBytes down to the last non-magic value */
static size_t nonMagicInBytes(const FContext& context) {
  uint64_t* begin = static_cast<uint64_t*>(context.stackLimit());
  uint64_t* end = static_cast<uint64_t*>(context.stackBase());

  auto firstNonMagic = std::find_if(
    begin, end,
    [](uint64_t val) {
      return val != kMagic8Bytes;
    }
  );

  return (end - firstNonMagic) * sizeof(uint64_t);
}

}  // anonymous namespace

void Fiber::setData(intptr_t data) {
  assert(state_ == AWAITING);
  data_ = data;
  state_ = READY_TO_RUN;

  if (LIKELY(threadId_ == localThreadId())) {
    TAILQ_INSERT_TAIL(&fiberManager_.readyFibers_, this, entry_);
    fiberManager_.ensureLoopScheduled();
  } else {
    fiberManager_.remoteReadyInsert(this);
  }
}

Fiber::Fiber(FiberManager& fiberManager) :
    fiberManager_(fiberManager) {

  auto size = fiberManager_.options_.stackSize;
  auto limit = fiberManager_.stackAllocator_.allocate(size);

  fcontext_ = makeContext(limit, size, &Fiber::fiberFuncHelper);

  if (UNLIKELY(fiberManager_.options_.debugRecordStackUsed)) {
    fillMagic(fcontext_);
  }
}

Fiber::~Fiber() {
  if (cleanupFunc_) {
    cleanupFunc_(context_);
  }
  fiberManager_.stackAllocator_.deallocate(
    static_cast<unsigned char*>(fcontext_.stackLimit()),
    fiberManager_.options_.stackSize);
}

void Fiber::recordStackPosition() {
  int stackDummy;
  fiberManager_.stackHighWatermark_ =
    std::max(fiberManager_.stackHighWatermark_,
             static_cast<size_t>(
               static_cast<unsigned char*>(fcontext_.stackBase()) -
               static_cast<unsigned char*>(
                 static_cast<void*>(&stackDummy))));
}

void Fiber::fiberFuncHelper(intptr_t fiber) {
  reinterpret_cast<Fiber*>(fiber)->fiberFunc();
}

void Fiber::fiberFunc() {
  while (1) {
    assert(state_ == NOT_STARTED);

    threadId_ = localThreadId();
    state_ = RUNNING;

    /* We need to allocate at least resultSize_ bytes with
       the least restrictive alignment requirement. */
    MaxAlign result[(resultSize_ + sizeof(MaxAlign) - 1) /
                    sizeof(MaxAlign)];

    intptr_t resultLoc = reinterpret_cast<intptr_t>(&result);

    try {
      if (resultFunc_) {
        assert(finallyFunc_);
        assert(!func_);

        resultFunc_(resultLoc, context_);
      } else {
        assert(func_);
        func_();
      }
    } catch (...) {
      fiberManager_.exceptionCallback_(std::current_exception());
    }

    if (UNLIKELY(fiberManager_.options_.debugRecordStackUsed)) {
      fiberManager_.stackHighWatermark_ =
        std::max(fiberManager_.stackHighWatermark_,
                 nonMagicInBytes(fcontext_));
    }

    state_ = INVALID;

    resultSize_ = 0;
    fiberManager_.activeFiber_ = nullptr;

    auto fiber = reinterpret_cast<Fiber*>(
      jumpContext(&fcontext_, &fiberManager_.mainContext_, resultLoc));
    assert(fiber == this);
  }
}

intptr_t Fiber::preempt(State state) {
  assert(fiberManager_.activeFiber_ == this);
  assert(state_ == RUNNING);
  assert(state != RUNNING);

  fiberManager_.activeFiber_ = nullptr;
  state_ = state;

  recordStackPosition();

  auto ret = jumpContext(&fcontext_, &fiberManager_.mainContext_, 0);

  assert(fiberManager_.activeFiber_ == this);
  assert(state_ == READY_TO_RUN);
  state_ = RUNNING;

  return ret;
}

void Fiber::setFunctionFinally(
  size_t resultSize,
  void (*resultFunc)(intptr_t resultLoc, intptr_t context),
  void (*finallyFunc)(intptr_t resultLoc, intptr_t context),
  intptr_t context,
  void (*cleanupFunc)(intptr_t context)) {

  assert(state_ == INVALID);
  resultSize_ = resultSize;
  resultFunc_ = resultFunc;
  finallyFunc_ = finallyFunc;
  context_ = context;
  cleanupFunc_ = cleanupFunc;
  state_ = NOT_STARTED;
}

}}
