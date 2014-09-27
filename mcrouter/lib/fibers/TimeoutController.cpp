/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "TimeoutController.h"

namespace facebook { namespace memcache {

TimeoutHandle::TimeoutHandle(std::function<void()> timeoutFunc) :
    timeoutFunc_(std::move(timeoutFunc)) {
}

bool TimeoutHandle::tryCancel() {
  if (listHook_.is_linked()) {
    listHook_.unlink();
    return true;
  }
  return false;
}

void TimeoutHandle::onTimeout() {
  timeoutFunc_();
}

TimeoutController::TimeoutController(LoopController& loopController) :
    nextTimeout_(TimePoint::max()),
    loopController_(loopController) {}

void TimeoutController::registerTimeout(TimeoutHandle& th, Duration duration) {
  auto& list = [&]() -> TimeoutHandleList& {
    for (auto& bucket : timeoutHandleBuckets_) {
      if (bucket.first == duration) {
        return bucket.second;
      }
    }

    timeoutHandleBuckets_.emplace_back(duration, TimeoutHandleList());
    return timeoutHandleBuckets_.back().second;
  }();

  list.push_back(th);

  th.timeout_ = Clock::now() + duration;
  if (th.timeout_ < nextTimeout_) {
    nextTimeout_ = th.timeout_;
    scheduleRun();
  }
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
    auto& list = bucket.second;

    for (auto it = list.begin(); it != list.end();) {
      if (it->timeout_ > time) {
        nextTimeout_ = std::min(nextTimeout_, it->timeout_);
        break;
      }

      it->onTimeout();
      it = list.erase(it);
    }
  }

  if (nextTimeout_ != TimePoint::max()) {
    scheduleRun();
  }
}

void TimeoutController::scheduleRun() {
  auto time = nextTimeout_;
  auto timeoutController = this;

  loopController_.timedSchedule([timeoutController, time]() {
      timeoutController->runTimeouts(time);
    }, time);
}

}}
