/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cassert>
#include <utility>

#include <folly/Conv.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"

namespace facebook {
namespace memcache {

/**
 * Route handle that can select a child from a list of children.
 *
 * @tparam RouterInfo   The router.
 * @tparam Selector     The selector, that has to implement
 *                      "type()" and "select()"
 */
template <class RouterInfo, typename Selector>
class SelectionRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;
  using RouteHandlePtr = typename RouterInfo::RouteHandlePtr;

 public:
  std::string routeName() const {
    return folly::to<std::string>("selection|", selector_.type());
  }

  /**
   * Constructs SelectionRoute.
   *
   * @param children                List of children route handles.
   * @param selector                Selector responsible for choosing to which
   *                                of the children the request should be sent
   *                                to.
   * @param outOfRangeDestination   The destination to which the request will be
   *                                routed if selector.select() returns a value
   *                                that is >= than children.size().
   */
  SelectionRoute(
      std::vector<RouteHandlePtr> children,
      Selector selector,
      RouteHandlePtr outOfRangeDestination)
      : children_(std::move(children)),
        selector_(std::move(selector)),
        outOfRangeDestination_(std::move(outOfRangeDestination)) {
    assert(!children_.empty());
    assert(outOfRangeDestination_);
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*select(req), req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    return select(req)->route(req);
  }

 private:
  const std::vector<RouteHandlePtr> children_;
  const Selector selector_;
  const RouteHandlePtr outOfRangeDestination_;

  template <class Request>
  RouteHandlePtr select(const Request& req) const {
    size_t idx = selector_.select(req, children_.size());
    if (idx >= children_.size()) {
      return outOfRangeDestination_;
    }
    return children_[idx];
  }
};

} // memcache
} // facebook
