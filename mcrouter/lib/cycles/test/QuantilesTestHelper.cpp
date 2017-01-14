/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/cycles/test/QuantilesTestHelper.h"

#include <folly/Random.h>

namespace facebook {
namespace memcache {
namespace cycles {
namespace test {

uint64_t normalRnd() {
  static std::mt19937 gen(folly::randomNumberSeed());
  std::normal_distribution<> d(10000, 50);
  return d(gen);
}
}
}
}
}
