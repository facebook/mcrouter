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

class TestContext {
 public:
  void setSenderId(size_t senderId) {
    senderId_ = senderId;
  }

  size_t senderId() const {
    return senderId_;
  }

 private:
  size_t senderId_{0};
};

class TestRouteHandleIf : public RouteHandleIf<TestRouteHandleIf,
                                               TestContext,
                                               List<McRequest>,
                                               McOpList> {
 public:
  using RouteHandleIf<TestRouteHandleIf,
                      TestContext,
                      List<McRequest>,
                      McOpList>::ContextPtr;

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type
  routeSimple(const Request& req, Operation) {
    return route(req, Operation(), nullptr);
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<TestRouteHandleIf>>
  couldRouteToSimple(const Request& req, Operation) {
    return couldRouteTo(req, Operation(), nullptr);
  }
};

typedef std::shared_ptr<TestRouteHandleIf> TestRouteHandlePtr;

template <typename Route>
using TestRouteHandle = RouteHandle<Route,
                                    TestRouteHandleIf,
                                    TestContext,
                                    List<McRequest>,
                                    McOpList>;

}}  // facebook::memcache
