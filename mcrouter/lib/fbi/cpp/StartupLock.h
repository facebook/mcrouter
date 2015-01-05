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

#include <condition_variable>
#include <mutex>

namespace facebook { namespace memcache {

/**
 * A synchronized primitive that lets you wait for initialization of
 * count separate threads. Threads should call notify when they are ready,
 * and other threads (or same threads, but after call to notify) may wait
 * for everybody to initialize via wait.
 */
class StartupLock {
public:
  explicit StartupLock(size_t count)
      : count_(count) {
  }

  void notify() {
    {
      std::lock_guard<std::mutex> lock(cvMutex_);
      if (count_ > 0) {
        --count_;
      }
    }
    cv_.notify_all();
  }

  void wait() {
    std::unique_lock<std::mutex> lock(cvMutex_);
    cv_.wait(lock, [this]() { return this->count_ == 0; });
  }

private:
  std::mutex cvMutex_;
  std::condition_variable cv_;
  size_t count_;
};

}}  // facebook::memcache
