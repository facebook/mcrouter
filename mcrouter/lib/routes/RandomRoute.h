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

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"

namespace facebook { namespace memcache {

/**
 * Sends the request to a random destination from list of children.
 */
template <class RouteHandleIf>
class RandomRoute {
 public:
  static std::string routeName() { return "random"; }

  template <class Operation, class Request>
  void traverse(const Request& req, Operation,
                const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(children_, req, Operation());
  }

  explicit RandomRoute(std::vector<std::shared_ptr<RouteHandleIf>> children)
      : children_(std::move(children)),
        gen_(std::ranlux24_base(
              std::chrono::system_clock::now().time_since_epoch().count())) {
    assert(!children_.empty());
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) {
    return children_[gen_() % children_.size()]->route(req, Operation());
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> children_;
  std::ranlux24_base gen_;
};

}}
