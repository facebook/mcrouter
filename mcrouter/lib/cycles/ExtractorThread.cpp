/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "ExtractorThread.h"

#include "mcrouter/lib/cycles/Accumulator.h"

namespace facebook { namespace memcache { namespace cycles { namespace detail {

ExtractorThread::~ExtractorThread() {
  stop();
}

bool ExtractorThread::running() const {
  return running_.load();
}

void ExtractorThread::start(std::function<void(CycleStats)> fn) {
  if (running_.exchange(true)) {
    return;
  }

  thread_ = std::thread([this, func = std::move(fn)] {
    while (running_.load()) {
      func(detail::extract());

      // Sleep
      {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait_for(lk, std::chrono::milliseconds(kExtractionIntervalMs),
                         [this] { return !running_.load(); });
      }
    }
  });
}

void ExtractorThread::stop() noexcept {
  if (running_.exchange(false)) {
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }
}

}}}} // namespace facebook::memcache::cycles::detail
