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

#include <memory>

#include "mcrouter/lib/test/RouteHandleTestUtil.h"
#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/ProxyRequestContext.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

using TestHandle = TestHandleImpl<McrouterRouteHandleIf>;

/**
 * Create mcrouter instance for test
 */
McrouterInstance* getTestRouter();

/**
 * Create recording ProxyRequestContext for fiber locals
 */
std::shared_ptr<ProxyRequestContext> getTestContext();

/**
 * Set valid McrouterFiberContext in fiber locals
 */
void mockFiberContext();

}}} // facebook::memcache::mcrouter
