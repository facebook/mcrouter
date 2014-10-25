/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <cassert>

#include <folly/Memory.h>
#include <folly/MoveWrapper.h>
#include <folly/Optional.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/wangle/Try.h>

#include "mcrouter/lib/fibers/Baton.h"
#include "mcrouter/lib/fibers/Fiber.h"
#include "mcrouter/lib/fibers/FiberPromise.h"
#include "mcrouter/lib/fibers/LoopController.h"

namespace facebook { namespace memcache {

inline void FiberManager::ensureLoopScheduled() {
  if (isLoopScheduled_) {
    return;
  }

  isLoopScheduled_ = true;
  loopController_->schedule();
}

inline void FiberManager::runReadyFiber(Fiber* fiber) {
  assert(fiber->state_ == Fiber::NOT_STARTED ||
         fiber->state_ == Fiber::READY_TO_RUN);

   while (fiber->state_ == Fiber::NOT_STARTED ||
         fiber->state_ == Fiber::READY_TO_RUN) {
    activeFiber_ = fiber;
    if (fiber->readyFunc_) {
      fiber->readyFunc_();
    }
    jumpContext(&mainContext_, &fiber->fcontext_, fiber->data_);
    if (fiber->state_ == Fiber::AWAITING_IMMEDIATE) {
      try {
        immediateFunc_();
      } catch (...) {
        exceptionCallback_(std::current_exception());
      }
      immediateFunc_ = nullptr;
      fiber->state_ = Fiber::READY_TO_RUN;
    }
  }

  if (fiber->state_ == Fiber::AWAITING) {
    awaitFunc_(*fiber);
    awaitFunc_ = nullptr;
  } else if (fiber->state_ == Fiber::INVALID) {
    assert(fibersActive_ > 0);
    --fibersActive_;
    // Making sure that task functor is deleted once task is complete.
    // NOTE: we must do it on main context, as the fiber is not
    // running at this point.
    fiber->func_ = nullptr;
    fiber->resultFunc_ = nullptr;
    if (fiber->finallyFunc_) {
      try {
        fiber->finallyFunc_();
      } catch (...) {
        exceptionCallback_(std::current_exception());
      }
      fiber->finallyFunc_ = nullptr;
    }

    if (fibersPoolSize_ < options_.maxFibersPoolSize) {
      TAILQ_INSERT_HEAD(&fibersPool_, fiber, entry_);
      ++fibersPoolSize_;
    } else {
      delete fiber;
      assert(fibersAllocated_ > 0);
      --fibersAllocated_;
    }
  }
}

inline bool FiberManager::loopUntilNoReady() {
  SCOPE_EXIT {
    isLoopScheduled_ = false;
    currentFiberManager_ = nullptr;
  };

  currentFiberManager_ = this;

  bool hadRemoteFiber = true;
  while (hadRemoteFiber) {
    hadRemoteFiber = false;

    while (!TAILQ_EMPTY(&readyFibers_)) {
      auto fiber = TAILQ_FIRST(&readyFibers_);
      TAILQ_REMOVE(&readyFibers_, fiber, entry_);
      runReadyFiber(fiber);
    }

    remoteReadyQueue_.sweep(
      [this, &hadRemoteFiber] (Fiber* fiber) {
        runReadyFiber(fiber);
        hadRemoteFiber = true;
      }
    );

    remoteTaskQueue_.sweep(
      [this, &hadRemoteFiber] (RemoteTask* taskPtr) {
        std::unique_ptr<RemoteTask> task(taskPtr);
        auto fiber = getFiber();
        fiber->setFunction(std::move(task->func));
        fiber->data_ = reinterpret_cast<intptr_t>(fiber);
        runReadyFiber(fiber);
        hadRemoteFiber = true;
      }
    );
  }

  return fibersActive_ > 0;
}

template <typename F>
void FiberManager::addTask(F&& func) {
  auto fiber = getFiber();
  fiber->setFunction(std::forward<F>(func));

  fiber->data_ = reinterpret_cast<intptr_t>(fiber);
  TAILQ_INSERT_TAIL(&readyFibers_, fiber, entry_);

  ensureLoopScheduled();
}

template <typename F, typename G>
void FiberManager::addTaskReadyFunc(F&& func, G&& readyFunc) {
  auto fiber = getFiber();
  fiber->setFunction(std::forward<F>(func));
  fiber->setReadyFunction(std::forward<G>(readyFunc));

  fiber->data_ = reinterpret_cast<intptr_t>(fiber);
  TAILQ_INSERT_TAIL(&readyFibers_, fiber, entry_);

  ensureLoopScheduled();
}

template <typename F>
void FiberManager::addTaskRemote(F&& func) {
  auto task = folly::make_unique<RemoteTask>(std::move(func));
  if (remoteTaskQueue_.insertHead(task.release())) {
    loopController_->scheduleThreadSafe();
  }
}

template <typename X>
struct IsRvalueRefTry { static const bool value = false; };
template <typename T>
struct IsRvalueRefTry<folly::wangle::Try<T>&&> { static const bool value = true; };

// We need this to be in a struct, not inlined in addTaskFinally, because clang
// crashes otherwise.
template <typename F, typename G>
struct FiberManager::AddTaskFinallyHelper {
  class Func;
  class Finally;

  typedef typename std::result_of<F()>::type Result;

  static constexpr bool allocateInBuffer =
    sizeof(Func) + sizeof(Finally) <= Fiber::kUserBufferSize;

  class Finally {
   public:
    Finally(G&& finally,
            FiberManager& fm) :
        finally_(std::move(finally)),
        fm_(fm) {
    }

    void operator()() {
      try {
        finally_(std::move(*result_));
      } catch (...) {
        fm_.exceptionCallback_(std::current_exception());
      }

      if (allocateInBuffer) {
        this->~Finally();
      } else {
        delete this;
      }
    }

   private:
    friend class Func;

    G finally_;
    folly::Optional<folly::wangle::Try<Result>> result_;
    FiberManager& fm_;
  };

  class Func {
   public:
    Func(F&& func, Finally& finally) :
        func_(std::move(func)), result_(finally.result_) {}

    void operator()() {
      result_ = folly::wangle::makeTryFunction(std::move(func_));

      if (allocateInBuffer) {
        this->~Func();
      } else {
        delete this;
      }
    }

   private:
    F func_;
    folly::Optional<folly::wangle::Try<Result>>& result_;
  };
};

template <typename F, typename G>
void FiberManager::addTaskFinally(F&& func, G&& finally) {
  typedef typename std::result_of<F()>::type Result;

  static_assert(
    IsRvalueRefTry<typename FirstArgOf<G>::type>::value,
    "finally(arg): arg must be Try<T>&&");
  static_assert(
    std::is_convertible<
      Result,
      typename std::remove_reference<
        typename FirstArgOf<G>::type
      >::type::element_type
    >::value,
    "finally(Try<T>&&): T must be convertible from func()'s return type");

  auto fiber = getFiber();

  typedef AddTaskFinallyHelper<F,G> Helper;

  if (Helper::allocateInBuffer) {
    auto funcLoc = static_cast<typename Helper::Func*>(
      fiber->getUserBuffer());
    auto finallyLoc = static_cast<typename Helper::Finally*>(
      static_cast<void*>(funcLoc + 1));

    new (finallyLoc) typename Helper::Finally(std::move(finally), *this);
    new (funcLoc) typename Helper::Func(std::move(func), *finallyLoc);

    fiber->setFunctionFinally(std::ref(*funcLoc), std::ref(*finallyLoc));
  } else {
    auto finallyLoc = new typename Helper::Finally(std::move(finally), *this);
    auto funcLoc = new typename Helper::Func(std::move(func), *finallyLoc);

    fiber->setFunctionFinally(std::ref(*funcLoc), std::ref(*finallyLoc));
  }

  fiber->data_ = reinterpret_cast<intptr_t>(fiber);
  TAILQ_INSERT_TAIL(&readyFibers_, fiber, entry_);

  ensureLoopScheduled();
}

template <typename F>
typename FirstArgOf<F>::type::value_type
FiberManager::await(F&& func) {

  typedef typename FirstArgOf<F>::type::value_type Result;

  folly::wangle::Try<Result> result;

  Baton baton;
  baton.wait([&func, &result, &baton]() mutable {
      func(FiberPromise<Result>(result, baton));
    });

  return folly::wangle::moveFromTry(std::move(result));
}

template <typename F>
typename std::result_of<F()>::type
FiberManager::runInMainContext(F&& func) {
  return runInMainContextHelper(std::forward<F>(func));
}

template <typename F>
inline typename std::enable_if<
  !std::is_same<typename std::result_of<F()>::type, void>::value,
  typename std::result_of<F()>::type>::type
FiberManager::runInMainContextHelper(F&& func) {
  if (UNLIKELY(activeFiber_ == nullptr)) {
    return func();
  }

  typedef typename std::result_of<F()>::type Result;

  folly::wangle::Try<Result> result;
  auto f = [&func, &result]() mutable {
    try {
      result = folly::wangle::Try<Result>(func());
    } catch (...) {
      result = folly::wangle::Try<Result>(std::current_exception());
    }
  };

  immediateFunc_ = std::ref(f);
  activeFiber_->preempt(Fiber::AWAITING_IMMEDIATE);

  return std::move(result.value());
}

template <typename F>
inline typename std::enable_if<
  std::is_same<typename std::result_of<F()>::type, void>::value,
  void>::type
FiberManager::runInMainContextHelper(F&& func) {
  if (UNLIKELY(activeFiber_ == nullptr)) {
    func();
    return;
  }

  immediateFunc_ = std::ref(func);
  activeFiber_->preempt(Fiber::AWAITING_IMMEDIATE);
}

inline FiberManager& FiberManager::getFiberManager() {
  assert(currentFiberManager_ != nullptr);
  return *currentFiberManager_;
}

inline FiberManager* FiberManager::getFiberManagerUnsafe() {
  return currentFiberManager_;
}

inline bool FiberManager::hasActiveFiber() {
  return activeFiber_ != nullptr;
}

}}
