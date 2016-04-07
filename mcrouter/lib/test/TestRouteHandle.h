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

#include "mcrouter/lib/fbi/cpp/TypeList.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/McRequestList.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/ThriftMessageList.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
#include "mcrouter/lib/RouteHandleIf.h"

namespace facebook { namespace memcache {

using TestRequestList = ConcatenateListsT<RequestList, ThriftRequestList>;

class TestRouteHandleIf : public RouteHandleIf<TestRouteHandleIf,
                                               TestRequestList> {
};

typedef std::shared_ptr<TestRouteHandleIf> TestRouteHandlePtr;

template <typename Route>
using TestRouteHandle = RouteHandle<Route,
                                    TestRouteHandleIf,
                                    TestRequestList>;

}}  // facebook::memcache
