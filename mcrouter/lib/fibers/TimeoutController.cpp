/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "TimeoutController.h"
#include <folly/Memory.h>

namespace facebook { namespace memcache {

TimeoutController::TimeoutController(LoopController& loopController) :
    nextTimeout_(TimePoint::max()),
    loopController_(loopController) {}

intptr_t TimeoutController::registerTimeout(std::function<void()> f,
                                            Duration duration) {
  auto& list = [&]() -> TimeoutHandleList& {
    for (auto& bucket : timeoutHandleBuckets_) {
      if (bucket.first == duration) {
        return *bucket.second;
      }
    }

    timeoutHandleBuckets_.emplace_back(duration,
                                       folly::make_unique<TimeoutHandleList>());
    return *timeoutHandleBuckets_.back().second;
  }();

  auto timeout = Clock::now() + duration;
  list.emplace(std::move(f), timeout, list);

  if (timeout < nextTimeout_) {
    nextTimeout_ = timeout;
    scheduleRun();
  }

  return reinterpret_cast<intptr_t>(&list.back());
}

void TimeoutController::runTimeouts(TimePoint time) {
  auto now = Clock::now();
  // Make sure we don't skip some events if function was run before actual time.
  if (time < now) {
    time = now;
  }
  if (nextTimeout_ > time) {
    return;
  }

  nextTimeout_ = TimePoint::max();

  for (auto& bucket : timeoutHandleBuckets_) {
    auto& list = *bucket.second;

    while (!list.empty()) {
      if (!list.front().canceled) {
        if (list.front().timeout > time) {
          nextTimeout_ = std::min(nextTimeout_, list.front().timeout);
          break;
        }

        list.front().func();
      }
      list.pop();
    }
  }

  if (nextTimeout_ != TimePoint::max()) {
    scheduleRun();
  }
}

void TimeoutController::scheduleRun() {
  auto time = nextTimeout_;
  std::weak_ptr<TimeoutController> timeoutControllerWeak = shared_from_this();

  loopController_.timedSchedule([timeoutControllerWeak, time]() {
      if (auto timeoutController = timeoutControllerWeak.lock()) {
        timeoutController->runTimeouts(time);
      }
    }, time);
}

void TimeoutController::cancel(intptr_t p) {
  auto handle = reinterpret_cast<TimeoutHandle*>(p);
  handle->canceled = true;

  auto& list = handle->list;

  while (!list.empty() && list.front().canceled) {
    list.pop();
  }
}

}}
