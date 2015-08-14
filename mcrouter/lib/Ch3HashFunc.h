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

#include <folly/Range.h>

#include "mcrouter/lib/fbi/hash.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

/* CH3 consistent hashing function object */
class Ch3HashFunc {
 public:
  explicit Ch3HashFunc(size_t n)
      : n_(n) {
    if (!n_ || n_ > furc_maximum_pool_size()) {
      throw std::logic_error("Pool size out of range for Ch3");
    }
  }

  size_t operator() (folly::StringPiece hashable) const {
    return furc_hash(hashable.data(), hashable.size(), n_);
  }

  static const char* type() {
    return "Ch3";
  }

 private:
  size_t n_;
};

}}  // facebook::memcache
