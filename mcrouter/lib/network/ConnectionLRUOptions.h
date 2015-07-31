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

namespace facebook { namespace memcache {

struct ConnectionLRUOptions {
  /**
   * Maximum number of connections per LRU.  If 0, the LRU logic is disabled
   * entirely. Default to disabled.
   */
  size_t maxConns{0};

  /**
   * Minimum age of the potential connection to evict.
   */
  std::chrono::milliseconds unreapableTime{1000000};

  /**
   * Minimum time between touches to update the LRU.
   */
  std::chrono::milliseconds updateThreshold{60000};
};

}}  // facebook::memcache
