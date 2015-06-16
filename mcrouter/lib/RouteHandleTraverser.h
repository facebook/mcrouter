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

#include <functional>
#include <memory>
#include <vector>

namespace facebook { namespace memcache {

/**
 * DFS over RouteHandle tree. Calls StartFunc before entering a node and
 * EndFunc after traversing over children of a node.
 */
template <class RouteHandleIf>
class RouteHandleTraverser {
 public:
  using StartFunc = std::function<void(const RouteHandleIf& r)>;
  using EndFunc = std::function<void()>;

  explicit RouteHandleTraverser(StartFunc start = nullptr,
                                EndFunc end = nullptr)
    : start_(std::move(start)),
      end_(std::move(end)) {
  }

  template <class Request, class Operation>
  void operator()(const RouteHandleIf& r, const Request& req, Operation) const {
    if (start_) {
      start_(r);
    }
    r.traverse(req, Operation(), *this);
    if (end_) {
      end_();
    }
  }

  template <class Request, class Operation>
  void operator()(const std::vector<std::shared_ptr<RouteHandleIf>>& v,
                  const Request& req, Operation) const {
    for (const auto& child : v) {
      operator()(*child, req, Operation());
    }
  }

 private:
  StartFunc start_;
  EndFunc end_;
};

}}  // facebook::memcache
