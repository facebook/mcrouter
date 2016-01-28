/*
 *  Copyright (c) 2016, Facebook, Inc.
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

uint64_t LeaseTokenMap::insert(std::string routeName, Item item) {
  std::lock_guard<std::mutex> lock(mutex_);

  uint64_t specialToken = applyMagic(nextId_++);

  auto it = data_.emplace(specialToken,
                          LeaseTokenMap::ListItem(specialToken,
                                                  std::move(routeName),
                                                  std::move(item),
                                                  leaseTokenTtlMs_));
  invalidationQueue_.push_back(it.first->second);

  return specialToken;
}

folly::Optional<LeaseTokenMap::Item> LeaseTokenMap::query(
    folly::StringPiece routeName, uint64_t token) {
  folly::Optional<LeaseTokenMap::Item> item;

  if (!hasMagic(token)) {
    return item;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(token);
    if (it != data_.end() && it->second.routeName == routeName) {
      item.emplace(std::move(it->second.item));
      data_.erase(it);
    }
  }

  return item;
}

uint64_t LeaseTokenMap::getOriginalLeaseToken(folly::StringPiece routeName,
                                              uint64_t token) const {
  if (!hasMagic(token)) {
    return token;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(token);
    if (it != data_.end() && it->second.routeName == routeName) {
      return it->second.item.originalToken;
    }
  }
  return token;
}

void LeaseTokenMap::onTimeout() {
  std::lock_guard<std::mutex> lock(mutex_);

  auto now = ListItem::Clock::now();
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
