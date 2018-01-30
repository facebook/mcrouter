/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

namespace facebook {
namespace memcache {
namespace mcrouter {

struct BigValueRouteOptions {
  constexpr explicit BigValueRouteOptions(size_t threshold_, size_t batchSize_)
      : threshold(threshold_), batchSize(batchSize_) {}
  const size_t threshold;
  const size_t batchSize;
};

} // mcrouter
} // memcache
} // facebook
