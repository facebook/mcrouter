/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CarbonRouterClientBase.h"

#include "mcrouter/lib/MessageQueue.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

CarbonRouterClientBase::CarbonRouterClientBase(
    size_t maximumOutstanding,
    bool maximumOutstandingError)
    : maxOutstanding_(maximumOutstanding),
      maxOutstandingError_(maximumOutstandingError) {
  static std::atomic<uint64_t> nextClientId(0ULL);
  clientId_ = nextClientId++;

  if (maxOutstanding_ != 0) {
    counting_sem_init(&outstandingReqsSem_, maxOutstanding_);
  }
}
}
}
} // facebook::memcache::mcrouter
