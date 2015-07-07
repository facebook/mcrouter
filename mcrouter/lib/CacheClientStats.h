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

#include <folly/detail/CacheLocality.h>
#include <folly/SpinLock.h>

namespace facebook { namespace memcache {

struct CacheClientCounters {
  size_t fetchCount{0};
  size_t fetchKeyBytes{0};
  size_t fetchValueBytes{0};
  size_t updateCount{0};
  size_t updateKeyBytes{0};
  size_t updateValueBytes{0};
  size_t invalidateCount{0};
  size_t invalidateKeyBytes{0};

  CacheClientCounters& operator+=(const CacheClientCounters& other) {
    fetchCount += other.fetchCount;
    fetchKeyBytes += other.fetchKeyBytes;
    fetchValueBytes += other.fetchValueBytes;
    updateCount += other.updateCount;
    updateKeyBytes += other.updateKeyBytes;
    updateValueBytes += other.updateValueBytes;
    invalidateCount += other.invalidateCount;
    invalidateKeyBytes += other.invalidateKeyBytes;

    return *this;
  }
};

class CacheClientStats {
 public:
  CacheClientCounters getCounters() const noexcept {
    folly::SpinLockGuard g(lock_);
    return counters_;
  }

  void recordFetchRequest(size_t keyBytes, size_t replyValueBytes) noexcept {
    folly::SpinLockGuard g(lock_);
    counters_.fetchCount++;
    counters_.fetchKeyBytes += keyBytes;
    counters_.fetchValueBytes += replyValueBytes;
  }

  void recordUpdateRequest(size_t keyBytes, size_t valueBytes) noexcept {
    folly::SpinLockGuard g(lock_);
    counters_.updateCount++;
    counters_.updateKeyBytes += keyBytes;
    counters_.updateValueBytes += valueBytes;
  }

  void recordInvalidateRequest(size_t keyBytes) noexcept {
    folly::SpinLockGuard g(lock_);
    counters_.invalidateCount++;
    counters_.invalidateKeyBytes += keyBytes;
  }

 private:
  mutable folly::SpinLock lock_ FOLLY_ALIGN_TO_AVOID_FALSE_SHARING;
  CacheClientCounters counters_;
};

}}  // facebook::memcache
