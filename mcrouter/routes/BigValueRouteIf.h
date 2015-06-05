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

namespace facebook { namespace memcache { namespace mcrouter {

struct BigValueRouteOptions {
  explicit BigValueRouteOptions(size_t threshold,
                                size_t batchSize) :
      threshold_(threshold),
      batchSize_(batchSize) {
  }
  const size_t threshold_;
  const size_t batchSize_;
};

}}}  // facebook::memcache::mcrouter
