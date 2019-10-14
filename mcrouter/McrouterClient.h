/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
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
