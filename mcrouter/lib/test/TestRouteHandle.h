/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/routes/McOpList.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/RouteHandleIf.h"

namespace facebook { namespace memcache {

typedef RouteOperationListT<McOperationList, List<McRequest>> TestOpList;

class TestRouteHandleIf : public RouteHandleIf<TestRouteHandleIf, TestOpList> {
};

typedef std::shared_ptr<TestRouteHandleIf> TestRouteHandlePtr;

template <typename Route>
using TestRouteHandle = RouteHandle<Route, TestRouteHandleIf, TestOpList>;

}}  // facebook::memcache
