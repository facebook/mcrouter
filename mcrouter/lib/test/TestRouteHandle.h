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

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/RouteHandleIf.h"
#include "mcrouter/routes/McOpList.h"

namespace facebook { namespace memcache {

class TestRouteHandleIf : public RouteHandleIf<TestRouteHandleIf,
                                               List<McRequest>,
                                               McOpList> {
};

typedef std::shared_ptr<TestRouteHandleIf> TestRouteHandlePtr;

template <typename Route>
using TestRouteHandle = RouteHandle<Route,
                                    TestRouteHandleIf,
                                    List<McRequest>,
                                    McOpList>;

}}  // facebook::memcache
