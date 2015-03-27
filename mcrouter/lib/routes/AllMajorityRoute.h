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

#include <folly/dynamic.h>

#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fibers/AddTasks.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/routes/NullRoute.h"

namespace facebook { namespace memcache {

/**
 * Sends the same request to all child route handles.
 * Collects replies until some result appears (half + 1) times
 * (or all results if that never happens).
 * Responds with one of the replies with the most common result.
 * Ties are broken using Reply::reduce().
 */
template <class RouteHandleIf>
class AllMajorityRoute {
 public:
  using ContextPtr = typename RouteHandleIf::ContextPtr;
  using StackContext = typename RouteHandleIf::StackContext;

  static std::string routeName() { return "all-majority"; }

  template <class Operation, class Request>
  std::vector<std::shared_ptr<RouteHandleIf>> couldRouteTo(
    const Request& req, Operation, const ContextPtr& ctx) const {

    return children_;
  }

  explicit AllMajorityRoute(std::vector<std::shared_ptr<RouteHandleIf>> rh)
      : children_(std::move(rh)) {
  }

  AllMajorityRoute(RouteHandleFactory<RouteHandleIf>& factory,
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
  typename ReplyType<Operation, Request>::type route(
    const Request& req, Operation, const ContextPtr& ctx,
    StackContext&& sctx) const {

    typedef typename ReplyType<Operation, Request>::type Reply;
    if (children_.empty()) {
      return NullRoute<RouteHandleIf>::route(req, Operation(), ctx,
                                             std::move(sctx));
    }

    /* Short circuit if one destination */
    if (children_.size() == 1) {
      return children_.back()->route(req, Operation(), ctx, std::move(sctx));
    }

    std::vector<std::function<Reply()>> funcs;
    funcs.reserve(children_.size());
    auto reqCopy = std::make_shared<Request>(req.clone());
    for (auto& rh : children_) {
#ifdef __clang__
#pragma clang diagnostic push // ignore generalized lambda capture warning
#pragma clang diagnostic ignored "-Wc++1y-extensions"
#endif
      funcs.push_back(
        [reqCopy, rh, ctx, sctx = StackContext(sctx)]() mutable {
          return rh->route(*reqCopy, Operation(), ctx, std::move(sctx));
        }
      );
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    }

    size_t counts[mc_nres];
    std::fill(counts, counts + mc_nres, 0);
    size_t majorityCount = 0;
    Reply majorityReply = Reply(DefaultReply, Operation());

    auto taskIt = fiber::addTasks(funcs.begin(), funcs.end());
    taskIt.reserve(children_.size() / 2 + 1);
    while (taskIt.hasNext() &&
           majorityCount < children_.size() / 2 + 1) {

      auto reply = taskIt.awaitNext();
      auto result = reply.result();

      ++counts[result];
      if ((counts[result] == majorityCount && reply.worseThan(majorityReply)) ||
          (counts[result] > majorityCount)) {
        majorityReply = std::move(reply);
        majorityCount = counts[result];
      }
    }

    return majorityReply;
  }

 private:
  std::vector<std::shared_ptr<RouteHandleIf>> children_;
};

}}
