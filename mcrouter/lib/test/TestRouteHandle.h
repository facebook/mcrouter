/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>

#include "mcrouter/lib/network/gen/MemcacheRouteHandleIf.h"

namespace facebook { namespace memcache {

using TestRouteHandleIf = MemcacheRouteHandleIf;

template <class Route>
using TestRouteHandle = MemcacheRouteHandle<Route>;

typedef std::shared_ptr<TestRouteHandleIf> TestRouteHandlePtr;

}}  // facebook::memcache
