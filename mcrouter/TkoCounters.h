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

#include <atomic>

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Number of boxes marked as TKO
 */
struct TkoCounters {
  std::atomic<size_t> softTkos{0};
  std::atomic<size_t> hardTkos{0};

  size_t totalTko() const {
    return softTkos + hardTkos;
  }
};

}}}  // facebook::memcache::mcrouter
