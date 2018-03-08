/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

namespace facebook {
namespace memcache {
namespace mcrouter {

enum class ProxyRequestPriority : uint8_t {
  kCritical = 0,
  kAsync = 1,
  kNumPriorities = 2,
};

} // mcrouter
} // memcache
} // facebook
