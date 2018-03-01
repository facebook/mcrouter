/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
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
  static std::string routeName() {
    return "loadbalancer";
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
   */
  LoadBalancerRoute(
      std::vector<std::shared_ptr<RouteHandleIf>> children,
      std::string salt,
      std::chrono::microseconds loadTtl,
      ServerLoad defaultServerLoad,
      size_t failoverCount)
      : children_(std::move(children)),
        salt_(std::move(salt)),
        loadTtl_(loadTtl),
        defaultServerLoadWeight_(
            defaultServerLoad.complement().percentLoad() / 100),
        failoverCount_(std::min(failoverCount, children_.size())),
        weightedLoads_(children_.size(), 1.0),
        expTimes_(children_.size(), std::chrono::microseconds(0)) {
    assert(!children_.empty());
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    size_t idx = select(req, weightedLoads_, salt_);
    t(*children_[idx], req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    // first try
    size_t idx = select(req, weightedLoads_, salt_);
    auto reply = doRoute(req, idx);

    // retry in case of error
    if (failoverCount_ > 1 && shouldFailover(reply)) {
      std::vector<double> weights = weightedLoads_;
      std::vector<size_t> indexMap(children_.size(), 0);
      std::iota(indexMap.begin(), indexMap.end(), 0);

      int64_t retries = failoverCount_;
      while (--retries > 0 && shouldFailover(reply)) {
        std::swap(weights[idx], weights.back());
        std::swap(indexMap[idx], indexMap.back());
        weights.pop_back();
        indexMap.pop_back();

        idx = select(req, weights, folly::to<std::string>(salt_, retries));
        reply = doRoute(req, indexMap[idx], /* isFailover */ true);
      }
    }

    // Reset expried loads.
    const int64_t now = nowUs();
    for (size_t i = 0; i < children_.size(); i++) {
      if (expTimes_[i].count() != 0 && expTimes_[i].count() <= now) {
        expTimes_[i] = std::chrono::microseconds(0);
        weightedLoads_[i] = defaultServerLoadWeight_;
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
  // value is reset to 'defaultServerLoad' value.
  const std::chrono::microseconds loadTtl_{100000};
  // Value to which server load is reset when it's value expires.
  const double defaultServerLoadWeight_{0.5};
  // Number of times to retry on error.
  const size_t failoverCount_{1};

  // 1-complement of the server load (i.e. 1 - serverLoad) of each of the
  // children.
  std::vector<double> weightedLoads_;
  // Point in time when the weightedLoad become too old to trusted.
  std::vector<std::chrono::microseconds> expTimes_;

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
            weightedLoads_[idx] = load.complement().percentLoad() / 100;

            // mark the current load of the server for expiration only if it is
            // more than default server load.
            // (using '<' below because weightedLoads_ is actually the
            // complement of the load)
            if (weightedLoads_[idx] < defaultServerLoadWeight_) {
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
  size_t selectInternal(
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

  template <class Request>
  size_t select(
      const Request& req,
      const std::vector<double>& weights,
      folly::StringPiece salt) const {
    // Hash functions can be stack-intensive so jump back to the main context
    return folly::fibers::runInMainContext([this, &req, &weights, &salt]() {
      return selectInternal(req, weights, salt);
    });
  }
};

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr createLoadBalancerRoute(
    const folly::dynamic& json,
    std::vector<typename RouterInfo::RouteHandlePtr> rh) {
  assert(json.isObject());

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

  return makeRouteHandleWithInfo<RouterInfo, LoadBalancerRoute>(
      std::move(rh),
      std::move(salt),
      loadTtl,
      defaultServerLoad,
      failoverCount);
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
  if (children.size() == 0) {
    return createNullRoute<typename RouterInfo::RouteHandleIf>();
  }
  if (children.size() == 1) {
    return std::move(children[0]);
  }
  return createLoadBalancerRoute<RouterInfo>(json, std::move(children));
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
