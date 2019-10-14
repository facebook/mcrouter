/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "mcrouter/lib/routes/CollectionRoute.h"

namespace facebook {
namespace memcache {

template <class Request>
class AllSyncCollector {
 public:
  using Reply = ReplyT<Request>;

  AllSyncCollector(const Request&, size_t) {}

  folly::Optional<Reply> initialReply() const {
    return folly::none;
  }

  folly::Optional<Reply> iter(const Reply& reply) {
    if (!finalReply_ ||
        worseThan(reply.result(), finalReply_.value().result())) {
      finalReply_ = reply;
    }

    return folly::none;
  }

  Reply finalReply() {
    return finalReply_.value();
  }

 private:
  folly::Optional<Reply> finalReply_;
  Request req_;
  size_t no_of_children_;
};

template <class RouterInfo>
class AllSyncCollectionRoute
    : public CollectionRoute<RouterInfo, AllSyncCollector> {
 public:
  std::string routeName() const {
    return "AllSyncCollectionRoute";
  }

  explicit AllSyncCollectionRoute(
      std::vector<typename RouterInfo::RouteHandlePtr> children)
      : CollectionRoute<RouterInfo, AllSyncCollector>(children) {
    assert(!children.empty());
  }
};

} // end namespace memcache
} // end namespace facebook
