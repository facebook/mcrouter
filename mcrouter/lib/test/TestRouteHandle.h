/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/network/gen/MemcacheRouteHandleIf.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"

namespace facebook {
namespace memcache {

using TestRouteHandleIf = MemcacheRouteHandleIf;
using TestRouterInfo = MemcacheRouterInfo;

template <class Route>
using TestRouteHandle = MemcacheRouteHandle<Route>;

} // memcache
} // facebook
