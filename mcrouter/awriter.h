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

#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include <folly/experimental/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>
#include <folly/Range.h>

#include "mcrouter/lib/fbi/queue.h"
#include "mcrouter/lib/fbi/cpp/sfrlock.h"

namespace facebook { namespace memcache { namespace mcrouter {

// Forward declaration.
struct awriter_entry_t;

struct awriter_callbacks_t {
  void (*completed)(awriter_entry_t*, int);
  int (*perform_write)(awriter_entry_t*);
};

struct awriter_entry_t {
  TAILQ_ENTRY(awriter_entry_t) links;
  void *context;
  const awriter_callbacks_t *callbacks;
};

class AsyncWriter {
 public:
  /**
   * @param maxQueueSize: maximum number of run requests in a queue. "run" will
   *                      fail if there is already maxQueueSize requests in
   *                      the queue. 0 means the queue is unlimited.
   */
  explicit AsyncWriter(size_t maxQueueSize = 0);

  /**
   * Starts the awriter thread
   *
   * @param threadName name for the writer thread
   *                   (will be truncated, if length is >15 characters).
   * @return true on success, false on failure (e.g. the thread is already
             running)
   */
  bool start(folly::StringPiece threadName);

  /**
   * Stops the awriter and waits for all the functions to finish.
   */
  void stop() noexcept;

  /**
   * @return true if writer can process requests, false otherwise.
   */
  bool isActive() const {
    return !stopped_;
  }

  /**
   * Add a function to the queue to run asynchronously.
   *
   * @return true on success, false on failure (e.g. when we hit the queue
             size limit)
   */
  bool run(std::function<void()> f);

  /**
   * Waits for all the functions to complete
   */
  ~AsyncWriter();
 private:
  const size_t maxQueueSize_;
  std::atomic<size_t> queueSize_{0};
  std::atomic<bool> stopped_{false};
  SFRLock runLock_;
  // process id of the parent thread (before fork)
  const pid_t pid_;

  folly::fibers::FiberManager fiberManager_;
  folly::EventBase eventBase_;
  std::thread thread_;
};

/**
 * @return true on success, false otherwise
 */
bool awriter_queue(AsyncWriter* w, awriter_entry_t *e);

}}} // facebook::memcache::mcrouter
