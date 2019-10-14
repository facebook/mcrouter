/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <vector>

#include <boost/dynamic_bitset.hpp>
#include <boost/iterator/iterator_facade.hpp>

#include <folly/dynamic.h>

#include "mcrouter/lib/Ch3HashFunc.h"
#include "mcrouter/lib/HashSelector.h"
#include "mcrouter/lib/Reply.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/lib/fbi/cpp/ParsingUtil.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

struct Stats {
  uint32_t num_collisions;
};

template <typename RouteHandleIf>
class FailoverInOrderPolicy {
 public:
  static constexpr bool optimizeNoFailoverRouteCase = true;
  using RouteHandlePtr = std::shared_ptr<RouteHandleIf>;

  FailoverInOrderPolicy(
      const std::vector<RouteHandlePtr>& children,
      const folly::dynamic&)
      : children_(children) {}

  template <class Request>
  class Iterator : public boost::iterator_facade<
                       Iterator<Request>,
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

    Stats getStats() const {
      return {0};
    }

   private:
    void increment() {
      ++id_;
    }

    bool equal(const Iterator<Request>& other) const {
      return id_ == other.id_;
    }

    RouteHandleIf& dereference() const {
      return *children_[id_];
    }

    friend class boost::iterator_core_access;

    const std::vector<RouteHandlePtr>& children_;
    size_t id_;
  };

  uint32_t maxErrorTries() const {
    return std::numeric_limits<uint32_t>::max();
  }

  template <class Request>
  Iterator<Request> begin(Request&) {
    return Iterator<Request>(children_, 0);
  }

  template <class Request>
  Iterator<Request> end(Request&) const {
    return Iterator<Request>(children_, children_.size());
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
class FailoverDeterministicOrderPolicy {
 public:
  static constexpr bool optimizeNoFailoverRouteCase = true;
  using RouteHandlePtr = std::shared_ptr<RouteHandleIf>;

  FailoverDeterministicOrderPolicy(
      const std::vector<std::shared_ptr<RouteHandleIf>>& children,
      const folly::dynamic& json)
      : children_(children) {
    checkLogic(
        json.isObject(),
        "Failover: DeterministicOrderPolicy config is not an object");

    auto jMaxTries = json.get_ptr("max_tries");
    checkLogic(
        jMaxTries != nullptr,
        "Failover: DeterministicOrderPolicy must specify 'max_tries' field");
    maxTries_ = static_cast<size_t>(
        parseInt(*jMaxTries, "max_tries", 1, children_.size()));

    auto jMaxErrorTries = json.get_ptr("max_error_tries");
    checkLogic(
        jMaxErrorTries != nullptr,
        "Failover: DeterministicOrderPolicy must specify"
        " 'max_error_tries' field");
    maxErrorTries_ = static_cast<size_t>(
        parseInt(*jMaxErrorTries, "max_error_tries", 1, maxTries_));

    checkLogic(
        maxErrorTries_ <= maxTries_,
        "Failover: DeterministicOrderPolicy 'max_error_tries' must be <= "
        "'max_tries'");

    funcType_ = Ch3HashFunc::type();
    if (auto jHash = json.get_ptr("hash")) {
      if (auto jsalt = jHash->get_ptr("salt")) {
        checkLogic(
            jsalt->isString(),
            "Failover: DeterministicOrderPolicy: salt is not a String");
        try {
          salt_ = std::stoi(jsalt->asString());
          checkLogic(
              std::to_string(salt_) == jsalt->asString(),
              "Failover: DeterministicOrderPolicy:salt should be integer string");
        } catch (const std::exception& e) {
          LOG(WARNING) << "salt is not integer, using 1 for salt";
          salt_ = 1; // default known value for deterministic behavior
        }
      } else {
        salt_ = 1; // default known value for deterministic behavior
      }
      if (auto jhashFunc = jHash->get_ptr("hash_func")) {
        checkLogic(
            jhashFunc->isString(),
            "Failover: DeterministicOrderPolicy: hash_func is not a string");
        funcType_ = jhashFunc->getString();
      }
      config_ = *jHash;
    } else {
      config_ = json;
    }
  }

  template <class Request>
  class Iterator : public boost::iterator_facade<
                       Iterator<Request>,
                       RouteHandleIf,
                       std::forward_iterator_tag> {
   public:
    Iterator(
        FailoverDeterministicOrderPolicy<RouteHandleIf>& failoverPolicy,
        folly::dynamic config,
        std::string funcType,
        uint32_t salt,
        uint32_t id,
        Request& req)
        : policy_(failoverPolicy),
          funcType_(std::move(funcType)),
          salt_(salt),
          config_(std::move(config)),
          id_(id),
          req_(req),
          usedIndexes_(policy_.children_.size()) {
      index_ = 0;
      usedIndexes_.set(index_);
    }

    size_t getTrueIndex() const {
      return index_;
    }

    Stats getStats() const {
      return {collisions_};
    }

   private:
    void increment() {
      uint32_t numAttempts = 0;
      auto nChildren = policy_.children_.size();
      constexpr uint32_t maxAttempts = 100;
      do {
        salt_++;
        // For now only Ch3Hash, and WeightedCh3Hash are supported
        if (funcType_ == Ch3HashFunc::type()) {
          index_ = HashSelector<Ch3HashFunc>(
                       std::to_string(salt_), Ch3HashFunc(nChildren))
                       .select(req_, nChildren);
        } else if (funcType_ == WeightedCh3HashFunc::type()) {
          WeightedCh3HashFunc func{config_, nChildren};
          index_ =
              HashSelector<WeightedCh3HashFunc>(std::to_string(salt_), func)
                  .select(req_, nChildren);
        } else {
          throwLogic("Unknown hash function: {}", funcType_);
        }
        collisions_++;
      } while (usedIndexes_.test(index_) && (numAttempts++ < maxAttempts));
      collisions_--;
      usedIndexes_.set(index_);
      ++id_;
    }

    bool equal(const Iterator<Request>& other) const {
      return id_ == other.id_;
    }

    RouteHandleIf& dereference() const {
      return *policy_.children_[index_];
    }

    friend class boost::iterator_core_access;
    FailoverDeterministicOrderPolicy<RouteHandleIf>& policy_;
    std::string funcType_;
    uint32_t salt_{0};
    folly::dynamic config_;
    size_t id_{0};
    const Request& req_;
    uint32_t collisions_{0};
    // usedIndexes_ is used to keep track of indexes that have already been
    // used and is useful in avoiding picking the same destinations again and
    // again
    boost::dynamic_bitset<> usedIndexes_;
    size_t index_;
  };

  template <class Request>
  Iterator<Request> begin(Request& req) {
    return Iterator<Request>(*this, config_, funcType_, salt_, 0, req);
  }

  template <class Request>
  Iterator<Request> end(Request& req) {
    return Iterator<Request>(*this, config_, funcType_, salt_, maxTries_, req);
  }

  uint32_t maxErrorTries() const {
    return maxErrorTries_;
  }

  // Returns the stat to increment when failover occurs.
  stat_name_t getFailoverStat() const {
    return failover_deterministic_order_policy_stat;
  }

  // Returns the stat when all failover destinations are exhausted.
  stat_name_t getFailoverFailedStat() const {
    return failover_deterministic_order_policy_failed_stat;
  }

 private:
  const std::vector<RouteHandlePtr>& children_;
  uint32_t maxTries_;
  uint32_t maxErrorTries_;
  folly::dynamic config_;
  std::string funcType_;
  uint32_t salt_{0};
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

  template <class Request>
  class Iterator : public boost::iterator_facade<
                       Iterator<Request>,
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

    Stats getStats() const {
      return {0};
    }

   private:
    void increment() {
      if (id_ == 0) {
        order_ = std::move(policy_.getLeastFailureRouteIndices());
      }
      ++id_;
    }

    bool equal(const Iterator<Request>& other) const {
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

  uint32_t maxErrorTries() const {
    return std::numeric_limits<uint32_t>::max();
  }

  template <class Request>
  Iterator<Request> begin(Request&) {
    return Iterator<Request>(*this, 0);
  }

  template <class Request>
  Iterator<Request> end(Request&) {
    return Iterator<Request>(*this, maxTries_);
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
