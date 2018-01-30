/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <vector>

#include <folly/dynamic.h>

#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/routes/FailoverRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

template <class RouteHandleIf>
std::vector<std::shared_ptr<RouteHandleIf>> getTargets(
    std::vector<std::shared_ptr<RouteHandleIf>> targets,
    size_t failoverCount,
    size_t threadId,
    std::vector<double> weights,
    folly::StringPiece salt) {
  std::vector<std::shared_ptr<RouteHandleIf>> failovers;
  failoverCount = std::min(failoverCount, targets.size());
  size_t hashKey = folly::hash::hash_combine(0, globals::hostid());
  if (threadId != 0) {
    hashKey = folly::hash::hash_combine(hashKey, threadId);
  }
  if (!salt.empty()) {
    hashKey = folly::Hash()(hashKey, salt);
  }
  for (size_t i = 0; i < failoverCount; ++i) {
    auto id = weightedCh3Hash(folly::to<std::string>(hashKey), weights);
    failovers.push_back(std::move(targets[id]));
    std::swap(targets[id], targets.back());
    targets.pop_back();
    std::swap(weights[id], weights.back());
    weights.pop_back();
    hashKey = folly::hash::hash_combine(hashKey, i);
  }
  return failovers;
}

} // detail

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr createLatestRoute(
    const folly::dynamic& json,
    std::vector<typename RouterInfo::RouteHandlePtr> targets,
    size_t threadId) {
  size_t failoverCount = 5;
  size_t failoverThreadId = 0;
  folly::StringPiece salt;

  if (json.isObject()) {
    if (auto jfailoverCount = json.get_ptr("failover_count")) {
      checkLogic(
          jfailoverCount->isInt(),
          "LatestRoute: failover_count is not an integer");
      failoverCount = jfailoverCount->getInt();
    }
    if (auto jsalt = json.get_ptr("salt")) {
      checkLogic(jsalt->isString(), "LatestRoute: salt is not a string");
      salt = jsalt->stringPiece();
    }
    if (auto jthreadLocalFailover = json.get_ptr("thread_local_failover")) {
      checkLogic(
          jthreadLocalFailover->isBool(),
          "LatestRoute: thread_local_failover is not a boolean");
      if (jthreadLocalFailover->getBool()) {
        failoverThreadId = threadId;
      }
    }
  }

  std::vector<double> weights;
  if (!json.isObject() || !json.count("weights")) {
    weights.resize(targets.size(), 1.0);
  } else {
    weights = ch3wParseWeights(json, targets.size());
  }
  return makeFailoverRouteDefault<RouterInfo, FailoverRoute>(
      json,
      detail::getTargets(
          std::move(targets),
          failoverCount,
          failoverThreadId,
          std::move(weights),
          salt));
}

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeLatestRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> children;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
  } else {
    children = factory.createList(json);
  }
  return createLatestRoute<RouterInfo>(
      json, std::move(children), factory.getThreadId());
}

template <class RouterInfo>
typename RouterInfo::RouteHandlePtr createLatestRoute(
    RouteHandleFactory<typename RouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json,
    std::vector<typename RouterInfo::RouteHandlePtr> children) {
  return createLatestRoute<RouterInfo>(
      json, std::move(children), factory.getThreadId());
}

} // mcrouter
} // memcache
} // facebook
