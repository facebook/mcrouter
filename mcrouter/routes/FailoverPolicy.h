/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <vector>

#include <boost/iterator/iterator_facade.hpp>

#include <folly/dynamic.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/fbi/cpp/ParsingUtil.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <typename RouteHandleIf>
class FailoverInOrderPolicy {
 public:
  static constexpr bool optimizeNoFailoverRouteCase = true;
  using RouteHandlePtr = std::shared_ptr<RouteHandleIf>;

  FailoverInOrderPolicy(
      const std::vector<RouteHandlePtr>& children,
      const folly::dynamic&)
      : children_(children) {}

  class Iterator : public boost::iterator_facade<
                       Iterator,
                       RouteHandleIf,
                       std::forward_iterator_tag> {
   public:
    Iterator(const std::vector<RouteHandlePtr>& children, size_t id)
        : children_(children), id_(id) {
      assert(children_.size() > 1);
    }

    size_t getTrueIndex() const {
      return id_;
    }

   private:
    void increment() {
      ++id_;
    }

    bool equal(const Iterator& other) const {
      return id_ == other.id_;
    }

    RouteHandleIf& dereference() const {
      return *children_[id_];
    }

    friend class boost::iterator_core_access;

    const std::vector<RouteHandlePtr>& children_;
    size_t id_;
  };

  Iterator begin() const {
    return Iterator(children_, 0);
  }

  Iterator end() const {
    return Iterator(children_, children_.size());
  }

  // Returns the stat to increment when failover occurs.
  stat_name_t getFailoverStat() const {
    return failover_inorder_policy_stat;
  }

  // Returns the stat when all failover destinations are exhausted.
  stat_name_t getFailoverFailedStat() const {
    return failover_inorder_policy_failed_stat;
  }

 private:
  const std::vector<RouteHandlePtr>& children_;
};

template <typename RouteHandleIf>
class FailoverLeastFailuresPolicy {
 public:
  static constexpr bool optimizeNoFailoverRouteCase = true;
  using RouteHandlePtr = std::shared_ptr<RouteHandleIf>;

  FailoverLeastFailuresPolicy(
      const std::vector<std::shared_ptr<RouteHandleIf>>& children,
      const folly::dynamic& policyConfig)
      : children_(children), recentErrorCount_(children_.size(), 0) {
    auto jMaxTries = policyConfig.get_ptr("max_tries");
    checkLogic(
        jMaxTries != nullptr,
        "Failover: LeastFailuresPolicy must specify 'max_tries' field");
    maxTries_ = static_cast<size_t>(
        parseInt(*jMaxTries, "max_tries", 1, children_.size()));
  }

  class ChildProxy {
   public:
    ChildProxy(
        FailoverLeastFailuresPolicy<RouteHandleIf>& failoverPolicy,
        size_t index)
        : failoverPolicy_(failoverPolicy), index_(index) {}

    template <class Request>
    ReplyT<Request> route(const Request& req) {
      auto& child = failoverPolicy_.children_[index_];
      auto reply = child->route(req);
      if (isErrorResult(reply.result())) {
        failoverPolicy_.recentErrorCount_[index_]++;
      } else {
        failoverPolicy_.recentErrorCount_[index_] = 0;
      }

      return reply;
    }

   private:
    FailoverLeastFailuresPolicy<RouteHandleIf>& failoverPolicy_;
    size_t index_;
  };

  class Iterator : public boost::iterator_facade<
                       Iterator,
                       ChildProxy,
                       std::forward_iterator_tag,
                       ChildProxy> {
   public:
    Iterator(
        FailoverLeastFailuresPolicy<RouteHandleIf>& failoverPolicy,
        size_t id)
        : policy_(failoverPolicy), id_(id) {}

    size_t getTrueIndex() const {
      return order_[id_];
    }

   private:
    void increment() {
      if (id_ == 0) {
        order_ = std::move(policy_.getLeastFailureRouteIndices());
      }
      ++id_;
    }

    bool equal(const Iterator& other) const {
      return id_ == other.id_;
    }

    ChildProxy dereference() const {
      return ChildProxy(policy_, id_ == 0 ? id_ : order_[id_]);
    }

    friend class boost::iterator_core_access;

    FailoverLeastFailuresPolicy<RouteHandleIf>& policy_;
    std::vector<size_t> order_;
    size_t id_;
  };

  Iterator begin() {
    return Iterator(*this, 0);
  }

  Iterator end() {
    return Iterator(*this, maxTries_);
  }

  // Returns the stat to increment when failover occurs.
  stat_name_t getFailoverStat() const {
    return failover_least_failures_policy_stat;
  }

  // Returns the stat when all failover destinations are exhausted.
  stat_name_t getFailoverFailedStat() const {
    return failover_least_failures_policy_failed_stat;
  }

 private:
  std::vector<size_t> getLeastFailureRouteIndices() const {
    std::vector<size_t> indices;
    for (size_t i = 0; i < recentErrorCount_.size(); ++i) {
      indices.push_back(i);
    }
    // 0th index always goes first.
    std::stable_sort(
        indices.begin() + 1, indices.end(), [this](size_t a, size_t b) {
          return recentErrorCount_[a] < recentErrorCount_[b];
        });
    indices.resize(maxTries_);

    return indices;
  }

  const std::vector<RouteHandlePtr>& children_;
  size_t maxTries_;

  std::vector<size_t> recentErrorCount_;
};
}
}
} // facebook::memcache::mcrouter
