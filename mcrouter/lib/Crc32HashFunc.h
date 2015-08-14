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

/* Crc32 : crc32 hashing function object */
class Crc32HashFunc {
 public:
  explicit Crc32HashFunc(size_t n)
      : n_(n) {}

  size_t operator() (folly::StringPiece hashable) const {
    auto res = crc32_hash(hashable.data(), hashable.size());
    return (res & 0x7fffffff) % n_;
  }

  static const char* type() {
    return "Crc32";
  }

 private:
  size_t n_;
};

}}  // facebook::memcache
