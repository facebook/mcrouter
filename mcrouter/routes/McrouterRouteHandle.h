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

#include <memory>

#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class Route>
using McrouterRouteHandle = MemcacheRouteHandle<Route>;

using McrouterRouteHandleIf = MemcacheRouteHandleIf;

using McrouterRouteHandlePtr = std::shared_ptr<McrouterRouteHandleIf>;

using McrouterRouterInfo = MemcacheRouterInfo;

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
