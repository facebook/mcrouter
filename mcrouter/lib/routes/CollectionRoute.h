/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

/**
 * This class represents the Collection API that abstracts out the
 * routine of visiting children and Collecting results via Collector class.
 * Any route that needs to be defined should inherit the CollectionRoute
 * class and implement their own Collector class using the following API.
 * The template for a Collector class should look something like this:
 *
 * template <class Request>
 * class Collector {
 * public:
 * using Reply = ReplyT<Request>;
 *
 * Collector(const Request&, size_t) {}
 *
 * // This function is called before visiting any children and can help
 * // exiting early by returning a reply. Returning folly::none should
 * // start visiting the children
 * folly::Optional<Reply> initialReply() const;
 *
 * // This function is called on reply from each children. This method can be
 * // used to accumulate result as different routes require different techniques
 * // for accummulating results.
 * folly::Optional<Reply> iter(const Reply& reply);
 *
 * // This function returns the reply after visiting all children.
 * Reply finalReply();
 *
 *  private:
 *   folly::Optional<Reply> finalReply_;
 *   Request req_;
 *   size_t no_of_children_;
 * };
 *
 * @tparam RouterInfo   The router.
 * @tparam Collector     The Collector, that has to implement the above API.
 */

#include <memory>
#include <string>
#include <vector>

#include <folly/fibers/AddTasks.h>

#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/RouteHandleTraverser.h"

namespace facebook {
namespace memcache {

template <class RouterInfo, template <class> class Collector>
class CollectionRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;
  using RouteHandlePtr = typename RouterInfo::RouteHandlePtr;

 public:
  explicit CollectionRoute(std::vector<RouteHandlePtr> children)
      : children_(std::move(children)) {}

  template <class Request>
  bool traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    return t(children_, req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) const {
    using Reply = ReplyT<Request>;

    std::vector<std::function<Reply()>> funcs;
    funcs.reserve(children_.size());
    auto reqCopy = std::make_shared<Request>(req);
    for (auto& rh : children_) {
      funcs.push_back([reqCopy, rh]() { return rh->route(*reqCopy); });
    }

    auto taskIt = folly::fibers::addTasks(funcs.begin(), funcs.end());

    Collector<Request> collector(std::move(req), children_.size());
    auto res = collector.initialReply();
    if (res.hasValue()) {
      return res.value();
    }

    while (taskIt.hasNext()) {
      res = collector.iter(taskIt.awaitNext());

      if (res.hasValue()) {
        return res.value();
      }
    }

    return collector.finalReply();
  }

 private:
  const std::vector<RouteHandlePtr> children_;
};

} // end namespace memcache
} // end namespace facebook
