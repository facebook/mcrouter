/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_CPP_SFRLOCK_H
#define FBI_CPP_SFRLOCK_H

#include "mcrouter/lib/fbi/sfrlock.h"

class SFRLock;

/**
 * @class SFRReadLock
 * @brief C++ sfrlock_t read-lock wrapper implements BasicLockable.
 *
 * An instance of SFRReadLock can only be retrived via SFRLock::readLock().
 */
class SFRReadLock {
 public:
  void lock() {
    sfrlock_rdlock(lock_);
  }
  void unlock() {
    sfrlock_rdunlock(lock_);
  }
 private:
  friend class SFRLock;

  explicit SFRReadLock(sfrlock_t* lck) : lock_(lck) {}
  sfrlock_t* lock_;
};

/**
 * @class SFRWriteLock
 * @brief C++ sfrlock_t write-lock wrapper which implements BasicLockable.
 *
 * An instance of SFRWriteLock can only be retrived via SFRLock::writeLock().
 */
class SFRWriteLock {
 public:
  void lock() {
    sfrlock_wrlock(lock_);
  }
  void unlock() {
    sfrlock_wrunlock(lock_);
  }
 private:
  friend class SFRLock;

  explicit SFRWriteLock(sfrlock_t* lck) : lock_(lck) {}
  sfrlock_t* lock_;
};

/**
 * @class SFRLock
 * @brief C++ sfrlock_t wrapper
 *
 * Keeps a single srflock_t and provides access to SFRReadLock and SFRWriteLock
 * wrapping it.
 *
 * Example:
 *   SFRLock lock;
 *   ...
 *   {
 *     std::lock_guard<SFRReadLock> lg(lock.readLock());
 *     ... // read data protected by lock
 *   }
 *   ...
 *   {
 *     std::lock_guard<SFRWriteLock> lg(lock.writeLock());
 *     ... // update data protected by lock
 *   }
 */
class SFRLock {
 public:
  SFRLock() : readLock_(&lock_), writeLock_(&lock_) {
    sfrlock_init(&lock_);
  }

  SFRLock(const SFRLock& other) = delete;
  SFRLock& operator=(const SFRLock& other) = delete;

  SFRReadLock& readLock() {
    return readLock_;
  }
  SFRWriteLock& writeLock() {
    return writeLock_;
  }
 private:
  sfrlock_t lock_;

  SFRReadLock readLock_;
  SFRWriteLock writeLock_;
};

#endif
