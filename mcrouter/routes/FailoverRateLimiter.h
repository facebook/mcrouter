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

#include "mcrouter/TokenBucket.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * TokenBucket rate limiter for failover route.
 * We rate limit failover requests over normal requests: rate 0.5 means
 * we'll failover at most 50% of all requests, doesn't matter how many
 * requests we send per second.
 */
class FailoverRateLimiter {
 public:
  FailoverRateLimiter(double rate, double burst)
    : tb_(rate, burst, /* allow `burst` requests at time 0 */ -1e6) {
  }

  /**
   * @param json  Rate limiting configuration; must be an object. Format:
   *                {
   *                  "rate": [0..1],
   *                  "burst": [1..INF]
   *                }
   *              Where rate and burst parameters are passed to
   *              the corresponding TokenBucket's constructor.
   *              If burst key is missing, burst is set to default (see .cpp).
   */
  explicit FailoverRateLimiter(const folly::dynamic& json);

  /**
   * Bumps total number of requests (both failed and successful)
   */
  void bumpTotalReqs() {
    ++totalReqs_;
  }

  /**
   * Consumes one failed request and checks if we didn't hit the limit
   *
   * @return true  if we didn't hit the failover limit, false otherwise
   */
  bool failoverAllowed() {
    return tb_.consume(1, totalReqs_);
  }
 private:
  TokenBucket tb_;
  size_t totalReqs_{0};
};

}}}  // facebook::memcache::mcrouter
