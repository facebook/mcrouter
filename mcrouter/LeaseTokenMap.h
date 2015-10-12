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

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/IntrusiveList.h>

namespace facebook { namespace memcache {

struct AccessPoint;

namespace mcrouter {

using AccessPointPtr = std::shared_ptr<const AccessPoint>;

/**
 * Class responsible for mapping lease-tokens to destinations.
 * All operations are thread-safe.
 */
class LeaseTokenMap {
 public:
  /**
   * Creates a LeaseTokenMap.
   *
   * @param evbThread         EventBase thread, responsible for timeouts.
   * @param leaseTokenTtl     How many milliseconds the lease token will live.
   *                          Must be greater than 0.
   */
  explicit LeaseTokenMap(folly::ScopedEventBaseThread& evbThread,
                         uint32_t leaseTokenTtl = 10000);
  ~LeaseTokenMap();

  /**
   * Inserts a lease token into the map and returns a special token.
   *
   * @param originalToken   Original token, returned by memcached.
   * @param accessPoint     Destination that requests with this token
   *                        should be redirected to.
   * @param serverTimeout   Timeout of the destination server.
   * @return                Special token, that should be returned to
   *                        the client.
   */
  uint64_t insert(uint64_t originalToken,
                  AccessPointPtr accessPoint,
                  std::chrono::milliseconds severTimeout);

  /**
   * Queries the map for a special token. If found, the entry is
   * deleted from the map.
   *
   * @param token           Lease token provided by the client.
   * @param originalToken   Output parameter containing the original token
   *                        returned by memcached.
   * @param accessPoint     Output parameter containing the destination
   *                        that this request should be redirected to.
   * @param serverTimeout   Output parameter containing the timeout
   *                        of the destination server.
   * @return                True if the token was found in the map.
   *                        False otherwise.
   */
  bool query(uint64_t token, uint64_t& originalToken,
             AccessPointPtr& accessPoint,
             std::chrono::milliseconds& serverTimeout);

  /**
   * Returns the original lease token (i.e. the lease token returned by
   * memcached).
   *
   * @param token   Lease token. Can be either a special token or an ordinary
   *                lease token.
   * @return        The original lease token (i.e. the one returned by
   *                memcached).
   */
  uint64_t getOriginalLeaseToken(uint64_t token) const;

  /**
   * Returns the size of the data structure (i.e. how many tokens are stored).
   */
  size_t size() const;

  /**
   * Tell whether an originalToken (i.e. token returned by memcached)
   * conflicts with specialToken space (tokens returned by this data
   * structure).
   */
  static bool conflicts(uint64_t originalToken);

 private:
  struct Item {
   public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    Item(uint64_t sToken, uint64_t oToken,
         AccessPointPtr ap,
         std::chrono::milliseconds servTimeout,
         uint32_t tokenTimeoutMs)
        : specialToken(sToken),
          originalToken(oToken),
          accessPoint(std::move(ap)),
          serverTimeout(servTimeout),
          tokenTimeout(Clock::now() +
                       std::chrono::milliseconds(tokenTimeoutMs)) {
    }

    uint64_t specialToken;
    uint64_t originalToken;
    AccessPointPtr accessPoint;
    std::chrono::milliseconds serverTimeout;
    TimePoint tokenTimeout;

    folly::IntrusiveListHook listHook;
  };

  // Hold the id of the next element to be inserted in the data structure.
  uint32_t nextId_{0};

  // Underlying data structure.
  std::unordered_map<uint64_t, Item> data_;
  // Keeps an in-order list of what should be invalidated.
  folly::IntrusiveList<Item, &Item::listHook> invalidationQueue_;
  // Mutex to synchronize access to underlying data structure
  mutable std::mutex mutex_;

  // Handles timeout
  class TimeoutHandler : public folly::AsyncTimeout {
   public:
    explicit TimeoutHandler(LeaseTokenMap& parent, folly::EventBase& evb)
        : folly::AsyncTimeout(&evb),
          parent_(parent) {
    }
    void timeoutExpired() noexcept override {
      parent_.onTimeout();
    }

   private:
    LeaseTokenMap& parent_;
  };
  folly::ScopedEventBaseThread& evbThread_;
  TimeoutHandler timeoutHandler_;
  uint32_t leaseTokenTtlMs_;

  void onTimeout();
};

}}} // facebook::memcache::mcrouter
