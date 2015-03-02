/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <chrono>
#include <functional>
#include <queue>

#include <boost/intrusive/list.hpp>

#include <folly/Likely.h>

#include "mcrouter/lib/fibers/LoopController.h"

namespace facebook { namespace memcache {

class TimeoutController :
      public std::enable_shared_from_this<TimeoutController> {
 public:
  typedef std::chrono::steady_clock Clock;
  typedef std::chrono::time_point<Clock> TimePoint;
  typedef Clock::duration Duration;

  explicit TimeoutController(LoopController& loopController);

  intptr_t registerTimeout(std::function<void()> f, Duration duration);
  void cancel(intptr_t id);

  void runTimeouts(TimePoint time);

 private:
  void scheduleRun();

  class TimeoutHandle;
  typedef std::queue<TimeoutHandle> TimeoutHandleList;
  typedef std::unique_ptr<TimeoutHandleList> TimeoutHandleListPtr;

  struct TimeoutHandle {
    TimeoutHandle(std::function<void()> func_,
                  TimePoint timeout_,
                  TimeoutHandleList& list_) :
        func(std::move(func_)), timeout(timeout_), list(list_) {}

    std::function<void()> func;
    bool canceled{false};
    TimePoint timeout;
    TimeoutHandleList& list;
  };

  std::vector<std::pair<Duration, TimeoutHandleListPtr>> timeoutHandleBuckets_;
  TimePoint nextTimeout_;
  LoopController& loopController_;
};

}}
