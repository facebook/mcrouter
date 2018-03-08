/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
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
