/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "AsyncWriter.h"

#include <folly/Range.h>
#include <folly/ThreadName.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/AsyncWriterEntry.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/lib/fbi/cpp/sfrlock.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

AsyncWriter::AsyncWriter(size_t maxQueueSize)
    : maxQueueSize_(maxQueueSize),
      fiberManager_(
          folly::make_unique<folly::fibers::EventBaseLoopController>()),
      eventBase_(/* enableTimeMeasurement */ false) {
  auto& c = fiberManager_.loopController();
  dynamic_cast<folly::fibers::EventBaseLoopController&>(c).attachEventBase(
      eventBase_);
}

AsyncWriter::~AsyncWriter() {
  stop();
  assert(!fiberManager_.hasTasks());
}

void AsyncWriter::stop() noexcept {
  {
    std::lock_guard<SFRWriteLock> lock(runLock_.writeLock());
    if (stopped_) {
      return;
    }
    stopped_ = true;
  }

  if (thread_.joinable()) {
    eventBase_.terminateLoopSoon();
    thread_.join();
  } else {
    while (fiberManager_.hasTasks()) {
      eventBase_.loopOnce();
    }
  }
}

bool AsyncWriter::start(folly::StringPiece threadName) {
  std::lock_guard<SFRWriteLock> lock(runLock_.writeLock());
  if (thread_.joinable() || stopped_) {
    return false;
  }

  try {
    thread_ = std::thread([this]() {
      // will return after terminateLoopSoon is called
      eventBase_.loopForever();

      while (fiberManager_.hasTasks()) {
        fiberManager_.loopUntilNoReady();
      }
    });
    folly::setThreadName(thread_.native_handle(), threadName);
  } catch (const std::system_error& e) {
    LOG_FAILURE(
        "mcrouter",
        memcache::failure::Category::kSystemError,
        "Can not start AsyncWriter thread {}: {}",
        threadName,
        e.what());
    return false;
  }

  return true;
}

bool AsyncWriter::run(std::function<void()> f) {
  std::lock_guard<SFRReadLock> lock(runLock_.readLock());
  if (stopped_) {
    return false;
  }

  if (maxQueueSize_ != 0) {
    auto size = queueSize_.load();
    do {
      if (maxQueueSize_ == size) {
        return false;
      }
    } while (!queueSize_.compare_exchange_weak(size, size + 1));
  }

  fiberManager_.addTaskRemote([ this, f_ = std::move(f) ]() {
    fiberManager_.runInMainContext(std::move(f_));
    if (maxQueueSize_ != 0) {
      --queueSize_;
    }
  });
  return true;
}

bool awriter_queue(AsyncWriter* w, awriter_entry_t* e) {
  return w->run([e, w]() {
    if (!w->isActive()) {
      e->callbacks->completed(e, EPIPE);
      return;
    }
    int r = e->callbacks->perform_write(e);
    e->callbacks->completed(e, r);
  });
}
}
}
} // facebook::memcache::mcrouter
