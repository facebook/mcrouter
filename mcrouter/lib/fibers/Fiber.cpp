/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "Fiber.h"

#include <unistd.h>
#include <sys/syscall.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

#include "folly/Likely.h"
#include "folly/Portability.h"
#include "mcrouter/lib/fibers/FiberManager.h"

namespace facebook { namespace memcache {

namespace {
static const uint64_t kMagic8Bytes = 0xfaceb00cfaceb00c;

pid_t localThreadId() {
  static __thread pid_t threadId = syscall(SYS_gettid);
  return threadId;
}

#if BOOST_VERSION >= 105200

typedef boost::context::stack_t Stack;
typedef boost::context::fcontext_t Context;

// we assume stack grows downwards
inline void* stackLimit(const Stack& stack) {
  return static_cast<unsigned char*>(stack.sp) - stack.size;
}

inline void* stackBase(const Stack& stack) {
  return stack.sp;
}

#else

typedef boost::ctx::stack_t Stack;
typedef boost::ctx::fcontext_t Context;

// we assume stack grows downwards
inline void* stackLimit(const Stack& stack) {
  return stack.limit;
}

inline void* stackBase(const Stack& stack) {
  return stack.base;
}

#endif

static void fillMagic(const Stack& stack) {
  uint64_t* begin = static_cast<uint64_t*>(stackLimit(stack));
  uint64_t* end = static_cast<uint64_t*>(stackBase(stack));

  std::fill(begin, end, kMagic8Bytes);
}

/* Size of the region from p + nBytes down to the last non-magic value */
static size_t nonMagicInBytes(const Stack& stack) {
  uint64_t* begin = static_cast<uint64_t*>(stackLimit(stack));
  uint64_t* end = static_cast<uint64_t*>(stackBase(stack));

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

#if BOOST_VERSION >= 105200
  contextPtr_ = boost::context::make_fcontext(
    limit + size, size, &Fiber::fiberFuncHelper);
#else
  contextImpl_.fc_stack.limit = limit;
  contextImpl_.fc_stack.base = limit + size;
  contextPtr_ = &contextImpl_;
  make_fcontext(contextPtr_, &Fiber::fiberFuncHelper);
#endif

  if (UNLIKELY(fiberManager_.options_.debugRecordStackUsed)) {
    fillMagic(contextPtr_->fc_stack);
  }
}

Fiber::~Fiber() {
  fiberManager_.stackAllocator_.deallocate(
    static_cast<unsigned char*>(stackLimit(contextPtr_->fc_stack)),
    fiberManager_.options_.stackSize);
}

void Fiber::recordStackPosition() {
  int stackDummy;
  fiberManager_.stackHighWatermark_ =
    std::max(fiberManager_.stackHighWatermark_,
             static_cast<size_t>(
               static_cast<unsigned char*>(stackBase(contextPtr_->fc_stack)) -
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

        resultFunc_(resultLoc);
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
                 nonMagicInBytes(contextPtr_->fc_stack));
    }

    state_ = INVALID;

    resultSize_ = 0;
    fiberManager_.activeFiber_ = nullptr;

    auto fiber = reinterpret_cast<Fiber*>(
      jump_fcontext(contextPtr_, &fiberManager_.mainContext_, resultLoc, true));
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

  auto ret = jump_fcontext(contextPtr_, &fiberManager_.mainContext_, 0, true);

  assert(fiberManager_.activeFiber_ == this);
  assert(state_ == READY_TO_RUN);
  state_ = RUNNING;

  return ret;
}

}}
