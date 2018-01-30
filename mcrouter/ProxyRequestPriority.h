/*
 *  Copyright (c) 2015-present, Facebook, Inc.
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

enum class ProxyRequestPriority : uint8_t {
  kCritical = 0,
  kAsync = 1,
  kNumPriorities = 2,
};

} // mcrouter
} // memcache
} // facebook
