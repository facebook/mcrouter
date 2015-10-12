/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "LeaseTokenMap.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

constexpr uint64_t kClearIdMask =   0xFFFFFFFF00000000;
constexpr uint64_t kAddMagicMask =  0x7aceb00c00000000;

inline bool hasMagic(uint64_t token) {
  return (token & kClearIdMask) == kAddMagicMask;
}
inline uint64_t applyMagic(uint32_t id) {
  return kAddMagicMask | id;
}

} // anonymous

LeaseTokenMap::LeaseTokenMap(folly::ScopedEventBaseThread& evbThread,
                             uint32_t leaseTokenTtl)
    : evbThread_(evbThread),
      timeoutHandler_(*this, *evbThread.getEventBase()),
      leaseTokenTtlMs_(leaseTokenTtl) {
  assert(leaseTokenTtlMs_ > 0);
  evbThread_.getEventBase()->runInEventBaseThread([this]() {
    timeoutHandler_.scheduleTimeout(leaseTokenTtlMs_);
  });
}

LeaseTokenMap::~LeaseTokenMap() {
  if (evbThread_.running()) {
    evbThread_.getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [this]() {
        timeoutHandler_.cancelTimeout();
      });
  }
}

uint64_t LeaseTokenMap::insert(uint64_t originalToken,
                               AccessPointPtr accessPoint,
                               std::chrono::milliseconds serverTimeout) {
  std::lock_guard<std::mutex> lock(mutex_);

  uint64_t specialToken = applyMagic(nextId_++);

  auto it = data_.emplace(specialToken,
                          LeaseTokenMap::Item(specialToken, originalToken,
                                              std::move(accessPoint),
                                              std::move(serverTimeout),
                                              leaseTokenTtlMs_));
  invalidationQueue_.push_back(it.first->second);

  return specialToken;
}

bool LeaseTokenMap::query(uint64_t token, uint64_t& originalToken,
                          AccessPointPtr& accessPoint,
                          std::chrono::milliseconds& serverTimeout) {
  if (!hasMagic(token)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto item = data_.find(token);
  if (item != data_.end()) {
    originalToken = item->second.originalToken;
    accessPoint = std::move(item->second.accessPoint);
    serverTimeout = std::move(item->second.serverTimeout);
    data_.erase(item);
    return true;
  }

  return false;
}

uint64_t LeaseTokenMap::getOriginalLeaseToken(uint64_t token) const {
  if (!hasMagic(token)) {
    return token;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto item = data_.find(token);
  if (item != data_.end()) {
    return item->second.originalToken;
  }
  return token;
}

void LeaseTokenMap::onTimeout() {
  std::lock_guard<std::mutex> lock(mutex_);

  auto now = Item::Clock::now();
  auto cur = invalidationQueue_.begin();
  while (cur != invalidationQueue_.end() && cur->tokenTimeout <= now) {
    uint64_t specialToken = cur->specialToken;
    cur = invalidationQueue_.erase(cur);
    data_.erase(specialToken);
  }

  if (invalidationQueue_.empty()) {
    timeoutHandler_.scheduleTimeout(leaseTokenTtlMs_);
  } else {
    auto nextExpiration = std::chrono::duration_cast<std::chrono::milliseconds>(
        invalidationQueue_.front().tokenTimeout - now).count();
    timeoutHandler_.scheduleTimeout(std::max<uint32_t>(nextExpiration, 1));
  }
}

size_t LeaseTokenMap::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_.size();
}

bool LeaseTokenMap::conflicts(uint64_t originalToken) {
  return hasMagic(originalToken);
}

}}} // facebook::memcache::mcrouter
