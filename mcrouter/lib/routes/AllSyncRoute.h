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
#include <string>
#include <vector>

#include <folly/Optional.h>
#include <folly/experimental/fibers/ForEach.h>

#include "mcrouter/lib/fbi/cpp/FuncGenerator.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache {

/**
 * Sends the same request to all child route handles.
 * Collects all the replies and responds with the "most awful" reply.
 */
template <class RouteHandleIf>
class AllSyncRoute {
 public:
  static std::string routeName() { return "all-sync"; }

  explicit AllSyncRoute(std::vector<std::shared_ptr<RouteHandleIf>> rh)
      : children_(std::move(rh)) {
  }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation) const {

    return children_;
  }

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation) const {

    typedef typename ReplyType<Operation, Request>::type Reply;

    if (children_.empty()) {
      return NullRoute<RouteHandleIf>::route(req, Operation());
    }

    /* Short circuit if one destination */
    if (children_.size() == 1) {
      return children_.back()->route(req, Operation());
    }

    const auto& children = children_;
    auto fs = makeFuncGenerator([&req, &children](size_t id) {
      return children[id]->route(req, Operation());
    }, children_.size());

    folly::Optional<Reply> reply;
    folly::fibers::forEach(fs.begin(), fs.end(),
                           [&reply] (size_t id, Reply newReply) {
      if (!reply || newReply.worseThan(reply.value())) {
        reply = std::move(newReply);
      }
    });
    return std::move(reply.value());
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> children_;
};

}}
