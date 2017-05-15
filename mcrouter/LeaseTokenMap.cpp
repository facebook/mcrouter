/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "LeaseTokenMap.h"

#include <folly/io/async/AsyncTimeout.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace {

constexpr uint64_t kClearIdMask = 0xFFFFFFFF00000000;
constexpr uint64_t kAddMagicMask = 0x7aceb00c00000000;

inline bool hasMagic(uint64_t token) {
  return (token & kClearIdMask) == kAddMagicMask;
}
inline uint64_t applyMagic(uint32_t id) {
  return kAddMagicMask | id;
}

} // anonymous

LeaseTokenMap::LeaseTokenMap(
    folly::EventBaseThread& evbThread,
    uint32_t leaseTokenTtl)
    : evbThread_(evbThread),
      leaseTokenTtlMs_(leaseTokenTtl) {
  assert(leaseTokenTtlMs_ > 0);
  auto& evb = *evbThread_.getEventBase();
  evb.runInEventBaseThread([ this, &evb = evb ]() {
    tokenCleanupTimeout_ = folly::AsyncTimeout::schedule(
        std::chrono::milliseconds(leaseTokenTtlMs_), evb, [this]() noexcept {
          tokenCleanupTimeout();
        });
  });
}

LeaseTokenMap::~LeaseTokenMap() {
  if (evbThread_.running()) {
    evbThread_.getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
        [this]() { tokenCleanupTimeout_.reset(); });
  }
}

uint64_t LeaseTokenMap::insert(std::string routeName, Item item) {
  std::lock_guard<std::mutex> lock(mutex_);

  uint64_t specialToken = applyMagic(nextId_++);

  auto it = data_.emplace(
      specialToken,
      LeaseTokenMap::ListItem(
          specialToken,
          std::move(routeName),
          std::move(item),
          leaseTokenTtlMs_));
  invalidationQueue_.push_back(it.first->second);

  return specialToken;
}

folly::Optional<LeaseTokenMap::Item> LeaseTokenMap::query(
    folly::StringPiece routeName,
    uint64_t token) {
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

uint64_t LeaseTokenMap::getOriginalLeaseToken(
    folly::StringPiece routeName,
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

void LeaseTokenMap::tokenCleanupTimeout() {
  const auto now = ListItem::Clock::now();
  uint32_t nextTimeoutMs = leaseTokenTtlMs_;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto cur = invalidationQueue_.begin();
    while (cur != invalidationQueue_.end() && cur->tokenTimeout <= now) {
      uint64_t specialToken = cur->specialToken;
      cur = invalidationQueue_.erase(cur);
      data_.erase(specialToken);
    }

    if (!invalidationQueue_.empty()) {
      nextTimeoutMs = std::max<uint32_t>(
          1,
          std::chrono::duration_cast<std::chrono::milliseconds>(
              invalidationQueue_.front().tokenTimeout - now)
              .count());
    }
  }

  tokenCleanupTimeout_->scheduleTimeout(nextTimeoutMs);
}

size_t LeaseTokenMap::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_.size();
}

bool LeaseTokenMap::conflicts(uint64_t originalToken) {
  return hasMagic(originalToken);
}

} // mcrouter
} // memcache
} // facebook
