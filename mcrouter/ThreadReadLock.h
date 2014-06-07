/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <sys/syscall.h>
#include <sys/types.h>

#include "mcrouter/lib/fbi/sfrlock.h"

// Silence compiler warnings about unused private members
template <typename T>
inline void USE(T) {}

/**
 * Recursion-safe wrapper around sfrlock that can only be used from
 * a single thread.
 *
 * Implements C++ BasicLockable concept, so can be used with std::lock_guard
 *
 * Example:
 * {
 *   std::lock_guard<ThreadReadLock> lock(proxy->proxyThreadConfigReadLock);
 *   // config is locked here
 *   {
 *     std::lock_guard<ThreadReadLock> lock(proxy->proxyThreadConfigReadLock);
 *     // still fine, no deadlock
 *   }
 * }
 * // config is unlocked
 */
class ThreadReadLock {
 public:

#pragma GCC diagnostic push // "lock" tends to shadow parameters and locals
#pragma GCC diagnostic ignored "-Wshadow"
  explicit ThreadReadLock(std::shared_ptr<sfrlock_t> lock)
      : lock_(lock), lockCount_(0) {
    assert(lock_.get() != nullptr);
  }
#pragma GCC diagnostic pop

  void lock() {
    USE(threadId_);
    assert(lockCount_ == 0 ? (threadId_ = syscall(SYS_gettid)) && true :
           threadId_ == syscall(SYS_gettid));

    assert(lockCount_ >= 0);
    if (!lockCount_) {
      sfrlock_rdlock(lock_.get());
    }
    ++lockCount_;
  }

  void unlock() {
    assert(syscall(SYS_gettid) == threadId_);

    --lockCount_;
    if (!lockCount_) {
      sfrlock_rdunlock(lock_.get());
    }
    assert(lockCount_ >= 0);
  }

 private:
  std::shared_ptr<sfrlock_t> lock_;
  int lockCount_;
  pid_t threadId_;
};
