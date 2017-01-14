/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/network/CarbonMessageList.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class Route>
using McrouterRouteHandle = MemcacheRouteHandle<Route>;

using McrouterRouteHandleIf = MemcacheRouteHandleIf;

typedef std::shared_ptr<McrouterRouteHandleIf> McrouterRouteHandlePtr;

using McrouterRouterInfo = MemcacheRouterInfo;
}
}
} // facebook::memcache::mcrouter
