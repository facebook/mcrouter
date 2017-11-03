/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cassert>
#include <fstream>
#include <string>
#include <utility>

#include <folly/Conv.h>

#include "mcrouter/McrouterFiberContext.h"
#include "mcrouter/lib/HashUtil.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/RouteHandleTraverser.h"
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
 * @tparam RouteHandleIf   The Route handle If.
 */
template <class RouteHandleIf>
class LoadBalancerRoute {
  using RouteHandlePtr = std::shared_ptr<RouteHandleIf>;

 public:
  static std::string routeName() {
    return "loadbalancer";
  }

  /**
   * Constructs LoadBalancerRoute.
   *
   * @param children                List of children route handles.
   * @param salt                    salt
   * @param loadTtl                 TTL for load in micro seconds
   * @param defaultServerLoad       default server load upon TLL expiration
   *
   * Initialize the weighted load to 1.0 - this means serverLoad is 0.
   */
  LoadBalancerRoute(
      std::vector<std::shared_ptr<RouteHandleIf>> children,
      std::string salt,
      std::chrono::microseconds loadTtl,
      ServerLoad defaultServerLoad)
      : children_(std::move(children)),
        salt_(std::move(salt)),
        weightedLoads_(children_.size(), 1.0),
        expTimes_(children_.size(), std::chrono::microseconds(0)),
        loadTtl_(loadTtl) {
    defaultServerLoadWeight_ =
        defaultServerLoad.complement().percentLoad() / 100;
    assert(!children_.empty());
  }

  template <class Request>
  void traverse(
      const Request& req,
      const RouteHandleTraverser<RouteHandleIf>& t) const {
    t(*children_[select(req)], req);
  }

  template <class Request>
  ReplyT<Request> route(const Request& req) {
    size_t idx = select(req);
    auto rep = children_[idx]->route(req);
    auto load = mcrouter::fiber_local<MemcacheRouterInfo>::getServerLoad();
    weightedLoads_[idx] = load.complement().percentLoad() / 100;

    auto now = nowUs();
    // Mark the current load of the server for expiration only if it is
    // more than default server load
    if (weightedLoads_[idx] < defaultServerLoadWeight_) {
      expTimes_[idx] = std::chrono::microseconds(now + loadTtl_.count());
    }

    // Number of servers in LoadBalancer Pool is expected to be small -
    // 10s of servers. So, it is OK to do the following O(N) loop
    for (size_t i = 0; i < children_.size(); i++) {
      if (i != idx && expTimes_[i].count() != 0 &&
          expTimes_[i].count() <= now) {
        expTimes_[i] = std::chrono::microseconds(0);
        weightedLoads_[i] = defaultServerLoadWeight_;
        if (auto& ctx =
                mcrouter::fiber_local<MemcacheRouterInfo>::getSharedCtx()) {
          ctx->proxy().stats().increment(load_balancer_load_reset_count_stat);
        }
      }
    }

    return rep;
  }

 private:
  const std::vector<std::shared_ptr<RouteHandleIf>> children_;
  const std::string salt_;
  std::vector<double> weightedLoads_;
  // Assign an expiry time to a server load, so that a server's load
  // gets reset to a default value upon expiry. This is to
  // avoid not using a server that reports a high value of load(eg 100%)
  // and never gets selected afterwards. This means the load value never
  // gets updated, which is not what we want. expTimes_ are in micro seconds.
  std::vector<std::chrono::microseconds> expTimes_;
  // Default TTL of a serer load is 100ms represented in micro-seconds.
  // This indicates the time after which a server load value is reset
  // to 'defServerLoad' value below.
  std::chrono::microseconds loadTtl_{100000};
  // default server load value is 50%.
  double defaultServerLoadWeight_{0.5};

  template <class Request>
  size_t selectInternal(const Request& req) const {
    size_t n = 0;
    if (salt_.empty()) {
      n = weightedCh3Hash(req.key().routingKey(), weightedLoads_);
    } else {
      n = hashWithSalt(
          req.key().routingKey(), salt_, [this](const folly::StringPiece sp) {
            return weightedCh3Hash(sp, weightedLoads_);
          });
    }
    if (UNLIKELY(n >= children_.size())) {
      throw std::runtime_error("index out of range");
    }
    return n;
  }

  template <class Request>
  size_t select(const Request& req) const {
    // Hash functions can be stack-intensive so jump back to the main context
    return folly::fibers::runInMainContext(
        [this, &req]() { return selectInternal(req); });
  }
};

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf> createLoadBalancerRoute(
    const folly::dynamic& json,
    std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> rh) {
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

  return makeRouteHandle<typename RouterInfo::RouteHandleIf, LoadBalancerRoute>(
      std::move(rh), std::move(salt), loadTtl, defaultServerLoad);
}

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeLoadBalancerRoute(
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
