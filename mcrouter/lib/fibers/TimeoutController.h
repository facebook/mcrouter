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

#include <boost/intrusive/list.hpp>

#include <folly/Likely.h>

#include "mcrouter/lib/fibers/LoopController.h"

namespace facebook { namespace memcache {

class TimeoutController;

class TimeoutHandle {
 public:
  typedef std::chrono::steady_clock Clock;
  typedef std::chrono::time_point<Clock> TimePoint;

  typedef boost::intrusive::list_member_hook<
    boost::intrusive::link_mode<boost::intrusive::auto_unlink>> ListHook;

  explicit TimeoutHandle(std::function<void()> timeoutFunc);
  bool tryCancel();

 private:
  friend class TimeoutController;

  void onTimeout();

  ListHook listHook_;
  std::function<void()> timeoutFunc_;
  TimePoint timeout_;
};

class TimeoutController {
 public:
  typedef TimeoutHandle::Clock Clock;
  typedef TimeoutHandle::TimePoint TimePoint;
  typedef Clock::duration Duration;

  explicit TimeoutController(LoopController& loopController);

  void registerTimeout(TimeoutHandle& timeoutHandle, Duration duration);

  void runTimeouts(TimePoint time);

 private:
  void scheduleRun();

  typedef boost::intrusive::member_hook<
    TimeoutHandle,
    TimeoutHandle::ListHook,
    &TimeoutHandle::listHook_> TimeoutHandleListHook;
  typedef boost::intrusive::list<
    TimeoutHandle,
    TimeoutHandleListHook,
    boost::intrusive::constant_time_size<false>> TimeoutHandleList;

  std::vector<std::pair<Duration, TimeoutHandleList>> timeoutHandleBuckets_;
  TimePoint nextTimeout_;
  LoopController& loopController_;
};

}}
