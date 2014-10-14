/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <folly/dynamic.h>
#include <folly/Optional.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fibers/WhenN.h"
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

  AllSyncRoute(RouteHandleFactory<RouteHandleIf>& factory,
               const folly::dynamic& json) {
    if (json.isObject()) {
      if (json.count("children")) {
        children_ = factory.createList(json["children"]);
      }
    } else {
      children_ = factory.createList(json);
    }
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

    std::vector<std::function<Reply()>> fs;
    fs.reserve(children_.size());
    for (auto& rh : children_) {
      // no need to copy the child and request, we will not return from method
      // until we get replies
      fs.emplace_back(
        [&rh, &req]() {
          return rh->route(req, Operation());
        }
      );
    }

    folly::Optional<Reply> reply;
    fiber::forEach(fs.begin(), fs.end(), [&reply] (size_t id, Reply newReply) {
      if (!reply || newReply.worseThan(reply.value())) {
        reply = std::move(newReply);
      }
    });
    return std::move(reply.value());
  }

 private:
  std::vector<std::shared_ptr<RouteHandleIf>> children_;
};

}}
