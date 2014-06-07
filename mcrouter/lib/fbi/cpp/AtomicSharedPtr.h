/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#ifndef FBI_CPP_ATOMIC_SHARED_PTR_H
#define FBI_CPP_ATOMIC_SHARED_PTR_H

#include <memory>
#include <mutex>

#include "mcrouter/lib/fbi/cpp/sfrlock.h"

namespace facebook { namespace memcache {

/**
 * A wrapper around T that's safe to read/update
 * from different threads.  Uses sfrlock, so it's most suitable
 * for frequent reads/infrequent and unfair writes (think config structures).
 */
template <typename T>
class AtomicWrapper {
 public:
  typedef T element_type;

  AtomicWrapper(const AtomicWrapper&) = delete;
  AtomicWrapper& operator=(const AtomicWrapper&) = delete;
  AtomicWrapper(AtomicWrapper&&) = delete;
  AtomicWrapper& operator=(AtomicWrapper&&) = delete;

  explicit AtomicWrapper(T&& data)
      : data_(std::move(data)) {
  }

  /**
   * Get a copy of stored data.
   */
  T get() {
    T copy;
    {
      std::lock_guard<SFRReadLock> lg(dataLock_.readLock());
      copy = data_;
    }
    return copy;
  }

  /**
   * Exchanges contents of stored data with the provided object.
   */
  void swap(T& data) {
    {
      std::lock_guard<SFRWriteLock> lg(dataLock_.writeLock());
      std::swap(data, data_);
    }
  }

  /**
   * Update the saved object.
   */
  void set(T data) {
    /* We call swap here instead of setting data_ directly to avoid
       destructing the old object (which might be expensive)
       while holding the write lock */
    swap(data);
  }

 private:
  SFRLock dataLock_;
  T data_;
};

template<class T>
using AtomicSharedPtr = AtomicWrapper<std::shared_ptr<const T>>;

}}

#endif
