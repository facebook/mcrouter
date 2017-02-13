/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <chrono>
#include <thread>

#include <folly/Singleton.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

template <typename DurationT>
struct SteadyClock {
  using duration_type = DurationT;
  static duration_type time_since_epoch() {
    return std::chrono::duration_cast<DurationT>(
        std::chrono::steady_clock::now().time_since_epoch());
  }
  static void sleep_for(const duration_type& sleep_duration) {
    /* sleep override */
    std::this_thread::sleep_for(sleep_duration);
  }
};

template <typename DurationT>
class MockableClockBase {
 public:
  using duration_type = DurationT;
  virtual duration_type time_since_epoch() const = 0;
  virtual void sleep_for(const duration_type& sleep_duration) = 0;
  virtual ~MockableClockBase() {}
};

template <typename DurationT>
class FakeClock : public MockableClockBase<DurationT> {
 public:
  using duration_type = typename MockableClockBase<DurationT>::duration_type;
  explicit FakeClock(duration_type ticks = duration_type::zero())
      : ticks_(ticks) {}
  duration_type time_since_epoch() const override {
    return this->ticks_;
  }
  void sleep_for(const duration_type& sleep_duration) override {
    this->ticks_ += sleep_duration;
  }

 private:
  duration_type ticks_;
};

template <typename DurationT>
class MockableSteadyClock : public MockableClockBase<DurationT> {
 public:
  using duration_type = typename MockableClockBase<DurationT>::duration_type;
  duration_type time_since_epoch() const override {
    return SteadyClock<duration_type>::time_since_epoch();
  }
  void sleep_for(const duration_type& sleep_duration) override {
    SteadyClock<duration_type>::sleep_for(sleep_duration);
  }
};

template <typename DurationT, typename Tag>
struct MockableClock {
  using duration_type = typename MockableClockBase<DurationT>::duration_type;
  using singleton_type = folly::Singleton<MockableClockBase<DurationT>, Tag>;
  static duration_type time_since_epoch() {
    auto obj = singleton_type::try_get();
    return obj ? obj->time_since_epoch()
               : MockableSteadyClock<duration_type>().time_since_epoch();
  }
  static void sleep_for(const duration_type& sleep_duration) {
    auto obj = singleton_type::try_get();
    if (obj) {
      obj->sleep_for(sleep_duration);
    }
  }
};
}
}
} // facebook::memcache::mcrouter
