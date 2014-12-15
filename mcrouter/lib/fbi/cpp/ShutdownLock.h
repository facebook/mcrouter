/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#ifndef FBI_CPP_SHUTDOWN_LOCK_H
#define FBI_CPP_SHUTDOWN_LOCK_H

#include <atomic>
#include <mutex>

#include <folly/detail/CacheLocality.h>

#include "mcrouter/lib/fbi/cpp/sfrlock.h"

namespace facebook { namespace memcache {

struct shutdown_started_exception : std::runtime_error {
  shutdown_started_exception()
      : std::runtime_error("Shutdown started") {}
};

/**
 * A synchronized shutdown primitive.
 *
 * Example:
 *
 *   void threadA(Instance& inst) {
 *     try {
 *       // obtains non-exclusive lock; guarantees that all shutdown()
 *       // calls will be blocked until we release the lock.
 *
 *       std::lock_guard<ShutdownLock> lg(inst.shutdownLock);
 *       // here we can be sure instance will not be shutdown on us
 *     } catch (...) {
 *       // shutdown started, not safe to do work
 *     }
 *   }
 *
 *   void threadB(Instance& inst) {
 *     // do some work ...
 *
 *     inst.shutdownOnce(
 *       [&inst]() {
 *         // This is the thread that was the first to call shutdown()
 *         inst.cleanupThatsSafeToCallOnlyOnce();
 *       });
 *     }
 *   }
 */
class ShutdownLock {
 public:
  void lock() {
    lock_.readLock().lock();
    if (shutdownStarted_) {
      lock_.readLock().unlock();
      throw shutdown_started_exception();
    }
  }

  void unlock() {
    lock_.readLock().unlock();
  }

  /**
   * All calls to this method are serialized.
   * @param func() will be run immediately for the first caller only.
   * @return true if this was the first shutdown call (and func() was run).
   */
  template <class F>
  bool shutdownOnce(F&& func) {
    {
      std::lock_guard<SFRWriteLock> lg(lock_.writeLock());
      if (shutdownStarted_) {
        return false;
      }
      shutdownStarted_ = true;
    }
    func();
    return true;
  }

  /**
   * @return True if shutdownOnce() has already been called.
   */
  bool shutdownStarted() {
    return shutdownStarted_.load();
  }

 private:
  SFRLock lock_;
  std::atomic<bool> FOLLY_ALIGN_TO_AVOID_FALSE_SHARING shutdownStarted_{false};
};

}}  // facebook::memcache

#endif
