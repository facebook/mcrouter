/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace facebook {
namespace memcache {

/**
 * DFS over RouteHandle tree. Calls StartFunc before entering a node and
 * EndFunc after traversing over children of a node.
 */
template <class RouteHandleIf>
class RouteHandleTraverser {
 public:
  using StartFunc = std::function<void(const RouteHandleIf& r)>;
  using EndFunc = std::function<void()>;

  explicit RouteHandleTraverser(
      StartFunc start = nullptr,
      EndFunc end = nullptr)
      : start_(std::move(start)), end_(std::move(end)) {}

  template <class Request>
  bool operator()(const RouteHandleIf& r, const Request& req) const {
    if (start_) {
      start_(r);
    }
    auto stopTraversal = r.traverse(req, *this);
    if (end_) {
      end_();
    }
    return stopTraversal;
  }

  /*
   * If a route handle traverse returns true, the RouteHandleTraverser
   * will exit early from the traversal.
   */
  template <class Request>
  bool operator()(
      const std::vector<std::shared_ptr<RouteHandleIf>>& v,
      const Request& req) const {
    for (const auto& child : v) {
      if (operator()(*child, req)) {
        return true;
      }
    }
    return false;
  }

 private:
  StartFunc start_;
  EndFunc end_;
};
}
} // facebook::memcache
