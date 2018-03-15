/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <algorithm>
#include <cassert>
#include <fstream>
#include <string>
#include <utility>

#include <folly/Conv.h>
#include <folly/Range.h>

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/HashUtil.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

/**
 * LoadBalancerRoute looks at the server loads of all the servers and chooses
 * the best one to send request to. Complement of the server loads of the each
 * of the servers is used as 'weight' to the WeightedCh3Hash function to
 * determine the next destination server.
 *
 * @tparam RouteHandleInfo   The Router
 */
template <class RouterInfo>
class LoadBalancerRoute {
 private:
  using RouteHandleIf = typename RouterInfo::RouteHandleIf;
  using RouteHandlePtr = typename RouterInfo::RouteHandlePtr;

 public:
  enum class AlgorithmType : std::uint8_t {
    WEIGHTED_HASHING = 1,
    TWO_RANDOM_CHOICES = 2,
  };

  static constexpr folly::StringPiece kWeightedHashing = "weighted-hashing";
  static constexpr folly::StringPiece kTwoRandomChoices = "two-random-choices";

  std::string routeName() const {
    return folly::to<std::string>(
        "loadbalancer|",
        algorithm_ == AlgorithmType::WEIGHTED_HASHING ? kWeightedHashing
                                                      : kTwoRandomChoices);
  }

  /**
   * Constructs LoadBalancerRoute.
   * Initializes the weighted load to 1.0. This means serverLoad is 0.
   *
   * @param children                List of children route handles.
   * @param salt                    salt
   * @param loadTtl                 TTL for load in micro seconds
   * @param defaultServerLoad       default server load upon TLL expiration
   * @param failoverCount           Number of times to route the request.
   *                                1 means no failover (just route once).
   *                                The value will be capped to
   *                                std::min(failoverCount, children.size())
   * @param algorithm               AlgorithmType
   * @param seed                    seed for random number generator used in
   *                                the two random choices algorithm.
   */
  LoadBalancerRoute(
      std::vector<std::shared_ptr<RouteHandleIf>> children,
      std::string salt,
      std::chrono::microseconds loadTtl,
      ServerLoad defaultServerLoad,
      size_t failoverCount,
      AlgorithmType algorithm = AlgorithmType::WEIGHTED_HASHING,
      uint32_t seed = nowUs())
      : children_(std::move(children)),
        salt_(std::move(salt)),
        loadTtl_(loadTtl),
        defaultLoadComplement_(
            defaultServerLoad.complement().percentLoad() / 100),
        failoverCount_(std::min(failoverCount, children_.size())),
        loadComplements_(children_.size(), 1.0),
        expTimes_(children_.size(), std::chrono::microseconds(0)),
        gen_(seed),
        algorithm_(algorithm) {
    assert(children_.size() >= 2);
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    // Walk all children
    t(children_, req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    if (algorithm_ == AlgorithmType::TWO_RANDOM_CHOICES) {
      return routeTwoRandomChoices(req);
    }
    // first try
    size_t idx = selectWeightedHashing(req, loadComplements_, salt_);
    auto reply = doRoute(req, idx);

    // retry in case of error
    if (failoverCount_ > 1 && shouldFailover(reply)) {
      std::vector<double> weights = loadComplements_;
      std::vector<size_t> indexMap(children_.size(), 0);
      std::iota(indexMap.begin(), indexMap.end(), 0);

      int64_t retries = failoverCount_;
      while (--retries > 0 && shouldFailover(reply)) {
        std::swap(weights[idx], weights.back());
        std::swap(indexMap[idx], indexMap.back());
        weights.pop_back();
        indexMap.pop_back();

        idx = selectWeightedHashing(
            req, weights, folly::to<std::string>(salt_, retries));
        reply = doRoute(req, indexMap[idx], /* isFailover */ true);
      }
    }

    // Reset expried loads.
    const int64_t now = nowUs();
    for (size_t i = 0; i < children_.size(); i++) {
      if (expTimes_[i].count() != 0 && expTimes_[i].count() <= now) {
        expTimes_[i] = std::chrono::microseconds(0);
        loadComplements_[i] = defaultLoadComplement_;
        if (auto& ctx = mcrouter::fiber_local<RouterInfo>::getSharedCtx()) {
          ctx->proxy().stats().increment(load_balancer_load_reset_count_stat);
        }
      }
    }

    return reply;
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> children_;
  const std::string salt_;
  // Default TTL of a serer load. Indicates the time after which a server load
  // value is reset to 'defaultLoadComplement_' value.
  const std::chrono::microseconds loadTtl_{100000};
  // Value to which server load is reset when it's value expires.
  const double defaultLoadComplement_{0.5};
  // Number of times to retry on error.
  const size_t failoverCount_{1};

  // 1-complement of the server load (i.e. 1 - serverLoad) of each of the
  // children.
  std::vector<double> loadComplements_;
  // Point in time when the loadComplement becomes too old to be trusted.
  std::vector<std::chrono::microseconds> expTimes_;
  // Random Number generator used for TwoRandomChoices algorithm
  std::ranlux24_base gen_;
  // Load balancing algorithm
  AlgorithmType algorithm_;

  // route the request and update server load.
  template <class Request>
  ReplyT<Request>
  doRoute(const Request& req, const size_t idx, bool isFailover = false) {
    assert(idx < children_.size());
    return mcrouter::fiber_local<RouterInfo>::runWithLocals(
        [this, &req, idx, isFailover]() {
          if (isFailover) {
            fiber_local<RouterInfo>::addRequestClass(RequestClass::kFailover);
          }
          auto reply = children_[idx]->route(req);
          auto load = mcrouter::fiber_local<RouterInfo>::getServerLoad();
          if (!load.isZero()) {
            loadComplements_[idx] = load.complement().percentLoad() / 100;

            // mark the current load of the server for expiration only if it is
            // more than default server load.
            // (using '<' below because loadComplements_ is actually the
            // complement of the load)
            if (loadComplements_[idx] < defaultLoadComplement_) {
              expTimes_[idx] =
                  std::chrono::microseconds(nowUs() + loadTtl_.count());
            }
          }
          return reply;
        });
  }

  template <class Reply>
  bool shouldFailover(const Reply& reply) {
    return isErrorResult(reply.result());
  }

  template <class Request>
  ReplyT<Request> routeTwoRandomChoices(const Request& req) {
    std::pair<size_t, size_t> idxs = selectTwoRandomChoices();
    auto rep = children_[idxs.first]->route(req);
    auto load = mcrouter::fiber_local<RouterInfo>::getServerLoad();
    loadComplements_[idxs.first] = load.complement().percentLoad() / 100;

    auto now = nowUs();
    // Mark the current load of the server for expiration only if it is
    // more than default server load
    if (loadComplements_[idxs.first] < defaultLoadComplement_) {
      expTimes_[idxs.first] = std::chrono::microseconds(now + loadTtl_.count());
    }

    // If the second entry, which had higher load that the first entry, has
    // already expired, reset the expiry time to zero and set the load to
    // the defaultLoadComplement_.
    if (expTimes_[idxs.second].count() != 0 &&
        expTimes_[idxs.second].count() <= now) {
      expTimes_[idxs.second] = std::chrono::microseconds(0);
      loadComplements_[idxs.second] = defaultLoadComplement_;
      if (auto& ctx = mcrouter::fiber_local<RouterInfo>::getSharedCtx()) {
        ctx->proxy().stats().increment(load_balancer_load_reset_count_stat);
      }
    }

    return rep;
  }

  template <class Request>
  size_t selectWeightedHashingInternal(
      const Request& req,
      const std::vector<double>& weights,
      folly::StringPiece salt) const {
    size_t n = 0;
    if (salt.empty()) {
      n = weightedCh3Hash(req.key().routingKey(), weights);
    } else {
      n = hashWithSalt(
          req.key().routingKey(),
          salt,
          [&weights](const folly::StringPiece sp) {
            return weightedCh3Hash(sp, weights);
          });
    }
    if (UNLIKELY(n >= children_.size())) {
      throw std::runtime_error("index out of range");
    }
    return n;
  }

  /**
   * Implements Two Random Choices algorithm
   *
   * @return       pair of randomly selected idxs. First idx is of the child
   *               with lowest load. The second idx is of the child with the
   *               highest load.
   *
   */
  std::pair<size_t, size_t> selectTwoRandomChoices() {
    uint32_t x = 0;
    uint32_t y = 1;
    if (children_.size() > 2) {
      x = gen_() % children_.size();
      y = gen_() % (children_.size() - 1);
      if (x == y) {
        y = children_.size() - 1;
      }
    }

    if (loadComplements_[x] > loadComplements_[y]) {
      return std::make_pair<size_t, size_t>(x, y);
    }
    return std::make_pair<size_t, size_t>(y, x);
  }

  template <class Request>
  size_t selectWeightedHashing(
      const Request& req,
      const std::vector<double>& weights,
      folly::StringPiece salt) const {
    // Hash functions can be stack-intensive so jump back to the main context
    return folly::fibers::runInMainContext([this, &req, &weights, &salt]() {
      return selectWeightedHashingInternal(req, weights, salt);
    });
  }
};

template <class RouterInfo>
constexpr folly::StringPiece LoadBalancerRoute<RouterInfo>::kWeightedHashing;
template <class RouterInfo>
constexpr folly::StringPiece LoadBalancerRoute<RouterInfo>::kTwoRandomChoices;

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr createLoadBalancerRoute(
    const folly::dynamic& json,
    std::vector<typename RouterInfo::RouteHandlePtr> rh) {
  assert(json.isObject());

  if (rh.size() == 0) {
    return createNullRoute<typename RouterInfo::RouteHandleIf>();
  }

  if (rh.size() == 1) {
    return std::move(rh[0]);
  }

  std::string salt;
  if (auto jSalt = json.get_ptr("salt")) {
    checkLogic(jSalt->isString(), "LoadBalancerRoute: salt is not a string");
    salt = jSalt->getString();
  }
  std::chrono::microseconds loadTtl(100 * 1000); // 100 milli seconds
  if (auto jLoadTtl = json.get_ptr("load_ttl_ms")) {
    checkLogic(
        jLoadTtl->isInt(), "LoadBalancerRoute: load_ttl_ms is not an integer");
    loadTtl = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::milliseconds(jLoadTtl->getInt()));
  }
  auto defaultServerLoad = ServerLoad::fromPercentLoad(50);
  if (auto jDefServerLoad = json.get_ptr("default_server_load_percent")) {
    checkLogic(
        jDefServerLoad->isInt(),
        "LoadBalancerRoute: default server load percent is not an integer");
    auto defServerLoad = jDefServerLoad->getInt();
    checkLogic(
        defServerLoad >= 0 && defServerLoad <= 100,
        "LoadBalancerRoute: default_server_load_percent must be"
        " an integer between 0 and 100");
    defaultServerLoad = ServerLoad::fromPercentLoad(defServerLoad);
  }
  size_t failoverCount = 1;
  if (auto jFailoverCount = json.get_ptr("failover_count")) {
    checkLogic(
        jFailoverCount->isInt(),
        "LoadBalancerRoute: failover_count is not an integer");
    failoverCount = jFailoverCount->getInt();
  }
  auto algorithm =
      LoadBalancerRoute<RouterInfo>::AlgorithmType::WEIGHTED_HASHING;
  std::string algorithmStr;
  /* Optional and will default to weighted hashing */
  if (auto jAlgorithm = json.get_ptr("algorithm")) {
    checkLogic(
        jAlgorithm->isString(), "LoadBalancerRoute: algorithm is not a string");
    algorithmStr = jAlgorithm->getString();
    if (algorithmStr == LoadBalancerRoute<RouterInfo>::kWeightedHashing) {
      algorithm =
          LoadBalancerRoute<RouterInfo>::AlgorithmType::WEIGHTED_HASHING;
    } else if (
        algorithmStr == LoadBalancerRoute<RouterInfo>::kTwoRandomChoices) {
      algorithm =
          LoadBalancerRoute<RouterInfo>::AlgorithmType::TWO_RANDOM_CHOICES;
    } else {
      throwLogic("Unknown algorithm: {}", algorithmStr);
    }
  }
  return makeRouteHandleWithInfo<RouterInfo, LoadBalancerRoute>(
      std::move(rh),
      std::move(salt),
      loadTtl,
      defaultServerLoad,
      failoverCount,
      algorithm);
}

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr createLoadBalancerRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& /* factory */,
    const folly::dynamic& json,
    std::vector<typename RouterInfo::RouteHandlePtr> rh) {
  return createLoadBalancerRoute<RouterInfo>(json, std::move(rh));
}

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr makeLoadBalancerRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  checkLogic(json.isObject(), "LoadBalancerRoute is not an object");

  auto jChildren = json.get_ptr("children");
  checkLogic(
      jChildren != nullptr,
      "LoadBalancerRoute: 'children' property is missing");

  auto children = factory.createList(*jChildren);
  return createLoadBalancerRoute<RouterInfo>(json, std::move(children));
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
