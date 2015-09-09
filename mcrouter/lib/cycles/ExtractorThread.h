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

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "mcrouter/lib/cycles/Cycles.h"

namespace facebook { namespace memcache { namespace cycles { namespace detail {

/**
 * Thread that will do the extraction.
 */
class ExtractorThread {
 public:
  /**
   * Destroy this object. Stops thread execution if it's running.
   */
  ~ExtractorThread();

  /**
   * Informs whether this thread is running.
   */
  bool running() const;

  /**
   * Start running the extractor thread.
   * Note: If the thread is already running, this method does nothing.
   *
   * @param func  Function that will receive the extracted data.
   */
  void start(std::function<void(CycleStats)> func);

  /**
   * Stop running the extractor thread.
   */
  void stop() noexcept;

 private:
  // Interval between data extractions in milliseconds.
  const size_t kExtractionIntervalMs = 1001;

  // Condition variable to wake up on shutdown.
  std::condition_variable cv_;

  // Mutex to wake up on shutdown.
  std::mutex mutex_;

  // Informs whether the extractor thread is running.
  std::atomic<bool> running_{false};

  // The thread itself
  std::thread thread_;
};

}}}} // namespace facebook::memcache::cycles::detail
