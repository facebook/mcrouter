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

#include <folly/dynamic.h>

#include "mcrouter/lib/Ch3HashFunc.h"
#include "mcrouter/lib/Crc32HashFunc.h"
#include "mcrouter/lib/HashSelector.h"
#include "mcrouter/lib/RendezvousHashFunc.h"
#include "mcrouter/lib/SelectionRouteFactory.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/lib/routes/SelectionRoute.h"
#include "mcrouter/routes/LatestRoute.h"
#include "mcrouter/routes/LoadBalancerRoute.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/ShardHashFunc.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class HashFunc>
HashSelector<HashFunc> createHashSelector(std::string salt, HashFunc func) {
  return HashSelector<HashFunc>(std::move(salt), std::move(func));
}

template <class RouterInfo, class HashFunc>
typename RouterInfo::RouteHandlePtr createHashRoute(
    std::vector<typename RouterInfo::RouteHandlePtr> rh,
    std::string salt,
    HashFunc func) {
  return createSelectionRoute<RouterInfo, HashSelector<HashFunc>>(
      std::move(rh),
      createHashSelector<HashFunc>(std::move(salt), std::move(func)));
}

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf> createHashRoute(
    const folly::dynamic& json,
    std::vector<std::shared_ptr<typename RouterInfo::RouteHandleIf>> rh,
    size_t threadId) {
  std::string salt;
  folly::StringPiece funcType = Ch3HashFunc::type();
  if (json.isObject()) {
    if (auto jsalt = json.get_ptr("salt")) {
      checkLogic(jsalt->isString(), "HashRoute: salt is not a string");
      salt = jsalt->getString();
    }
    if (auto jhashFunc = json.get_ptr("hash_func")) {
      checkLogic(jhashFunc->isString(), "HashRoute: hash_func is not a string");
      funcType = jhashFunc->stringPiece();
    }
  }

  auto n = rh.size();
  if (n == 0) {
    return createNullRoute<typename RouterInfo::RouteHandleIf>();
  }
  if (n == 1) {
    return std::move(rh[0]);
  }

  if (funcType == Ch3HashFunc::type()) {
    return createHashRoute<RouterInfo, Ch3HashFunc>(
        std::move(rh), std::move(salt), Ch3HashFunc(n));
  } else if (funcType == Crc32HashFunc::type()) {
    return createHashRoute<RouterInfo, Crc32HashFunc>(
        std::move(rh), std::move(salt), Crc32HashFunc(n));
  } else if (funcType == WeightedCh3HashFunc::type()) {
    WeightedCh3HashFunc func{json, n};
    return createHashRoute<RouterInfo, WeightedCh3HashFunc>(
        std::move(rh), std::move(salt), std::move(func));
  } else if (funcType == ConstShardHashFunc::type()) {
    return createHashRoute<RouterInfo, ConstShardHashFunc>(
        std::move(rh), std::move(salt), ConstShardHashFunc(n));
  } else if (funcType == RendezvousHashFunc::type()) {
    std::vector<folly::StringPiece> endpoints;

    auto jtags = json.get_ptr("tags");
    checkLogic(jtags, "HashRoute: tags needed for Rendezvous hash route");
    checkLogic(jtags->isArray(), "HashRoute: tags is not an array");
    checkLogic(
        jtags->size() == rh.size(),
        "HashRoute: number of tags doesn't match number of route handles");

    for (const auto& jtag : *jtags) {
      checkLogic(jtag.isString(), "HashRoute: tag is not a string");
      endpoints.push_back(jtag.stringPiece());
    }

    return createHashRoute<RouterInfo, RendezvousHashFunc>(
        std::move(rh),
        std::move(salt),
        RendezvousHashFunc(std::move(endpoints)));
  } else if (funcType == "Latest") {
    return createLatestRoute<RouterInfo>(json, std::move(rh), threadId);
  } else if (funcType == "LoadBalancer") {
    return createLoadBalancerRoute<RouterInfo>(json, std::move(rh));
  }
  throwLogic("Unknown hash function: {}", funcType);
}

template <class RouterInfo>
std::shared_ptr<typename RouterInfo::RouteHandleIf> makeHashRoute(
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
  return createHashRoute<RouterInfo>(
      json, std::move(children), factory.getThreadId());
}

} // mcrouter
} // memcache
} // facebook
