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

#include <folly/Optional.h>

#include "mcrouter/lib/McOperationTraits.h"
#include "mcrouter/TokenBucket.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * This is a container for TokenBucket rate limiters for different
 * operation types.
 */
class RateLimiter {
 public:
  /**
   * @param json  Rate limiting configuration; must be an object. Format:
   *
   *              { "gets_rate": GR, "gets_burst": GB,
   *                "sets_rate": SR, "sets_burst": GB,
   *                "deletes_rate": DR, "deletes_burst": DB }
   *
   *              Where rate and burst parameters are passed to
   *              the corresponding TokenBucket's constructor.
   *              If some *_rate key is missing, no rate limiting is
   *              performed for that operation.
   *              If some *_burst key is missing, burst is set
   *              equal to rate.
   */
  explicit RateLimiter(const folly::dynamic& json);

  template <class Operation>
  bool canPassThrough(Operation, typename GetLike<Operation>::Type = 0) {
    return LIKELY(
      !getsTb_ || getsTb_->consume(1.0, TokenBucket::defaultClockNow()));
  }

  template <class Operation>
  bool canPassThrough(Operation, typename UpdateLike<Operation>::Type = 0) {
    return LIKELY(
      !setsTb_ || setsTb_->consume(1.0, TokenBucket::defaultClockNow()));
  }

  template <class Operation>
  bool canPassThrough(Operation, typename DeleteLike<Operation>::Type = 0) {
    return LIKELY(
      !deletesTb_ || deletesTb_->consume(1.0, TokenBucket::defaultClockNow()));
  }

  template <class Operation>
  bool canPassThrough(Operation, OtherThanT(Operation,
                                            GetLike<>,
                                            UpdateLike<>,
                                            DeleteLike<>) = 0) {
    return true;
  }

 private:
  folly::Optional<TokenBucket> getsTb_;
  folly::Optional<TokenBucket> setsTb_;
  folly::Optional<TokenBucket> deletesTb_;
};

}}}  // facebook::memcache::mcrouter
