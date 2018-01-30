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

#include "mcrouter/CarbonRouterClient.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

class McrouterClient {
 public:
  using Pointer = typename CarbonRouterClient<MemcacheRouterInfo>::Pointer;
};

} // mcrouter
} // memcache
} // facebook
