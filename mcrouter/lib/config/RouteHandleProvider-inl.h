/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/lib/Ch3HashFunc.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/Crc32HashFunc.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"

namespace facebook { namespace memcache {

template <class RouteHandleIf,
          template <typename... Ignored> class R,
          typename... RArgs,
          typename... Args>
std::shared_ptr<RouteHandleIf> makeRouteHandle(Args&&... args);

template <class RouteHandleIf>
class AllAsyncRoute;

template <class RouteHandleIf>
class AllFastestRoute;

template <class RouteHandleIf>
class AllInitialRoute;

template <class RouteHandleIf>
class AllMajorityRoute;

template <class RouteHandleIf>
class AllSyncRoute;

template <class RouteHandleIf>
class ErrorRoute;

template <class RouteHandleIf>
class FailoverRoute;

template <class RouteHandleIf, typename HashFunc>
class HashRoute;

template <class RouteHandleIf>
class HostIdRoute;

template <class RouteHandleIf>
class LatestRoute;

template <class RouteHandleIf>
class MissFailoverRoute;

template <class RouteHandleIf>
class NullRoute;

template <class RouteHandleIf>
class RandomRoute;

template <class RouteHandleIf>
std::vector<std::shared_ptr<RouteHandleIf>>
RouteHandleProvider<RouteHandleIf>::create(
    RouteHandleFactory<RouteHandleIf>& factory,
    folly::StringPiece type,
    const folly::dynamic& json) {

  if (type == "AllAsyncRoute") {
    return { makeRouteHandle<RouteHandleIf,
                             AllAsyncRoute>(factory, json) };
  } else if (type == "AllFastestRoute") {
    return { makeRouteHandle<RouteHandleIf,
                             AllFastestRoute>(factory, json) };
  } else if (type == "AllInitialRoute") {
    return { makeRouteHandle<RouteHandleIf,
                             AllInitialRoute>(factory, json) };
  } else if (type == "AllMajorityRoute") {
    return { makeRouteHandle<RouteHandleIf,
                             AllMajorityRoute>(factory, json) };
  } else if (type == "AllSyncRoute") {
    return { makeRouteHandle<RouteHandleIf, AllSyncRoute>(factory, json) };
  } else if (type == "ErrorRoute") {
    return { makeRouteHandle<RouteHandleIf, ErrorRoute>(json) };
  } else if (type == "FailoverRoute") {
    return { makeRouteHandle<RouteHandleIf, FailoverRoute>(factory, json) };
  } else if (type == "HashRoute") {
    std::vector<std::shared_ptr<RouteHandleIf>> children;
    if (!json.isObject()) {
      children = factory.createList(json);
    } else if (json.count("children")) {
      children = factory.createList(json["children"]);
    }
    return { makeHash(json, std::move(children)) };
  } else if (type == "HostIdRoute") {
    return { makeRouteHandle<RouteHandleIf, HostIdRoute>(factory, json) };
  } else if (type == "LatestRoute") {
    std::vector<std::shared_ptr<RouteHandleIf>> children;
    if (!json.isObject()) {
      children = factory.createList(json);
    } else if (json.count("children")) {
      children = factory.createList(json["children"]);
    }
    return { makeRouteHandle<RouteHandleIf, LatestRoute>(json,
                                                         std::move(children)) };
  } else if (type == "MissFailoverRoute") {
    return { makeRouteHandle<RouteHandleIf, MissFailoverRoute>(factory, json) };
  } else if (type == "NullRoute") {
    return { makeRouteHandle<RouteHandleIf, NullRoute>() };
  } else if (type == "RandomRoute") {
    return { makeRouteHandle<RouteHandleIf, RandomRoute>(factory, json) };
  }

  return {};
}

template <class RouteHandleIf>
std::shared_ptr<RouteHandleIf>
RouteHandleProvider<RouteHandleIf>::createHash(
    folly::StringPiece funcType,
    const folly::dynamic& json,
    std::vector<std::shared_ptr<RouteHandleIf>> children) {

  if (funcType == Ch3HashFunc::type()) {
    return makeRouteHandle<RouteHandleIf, HashRoute, Ch3HashFunc>(
      json, std::move(children));
  }
  if (funcType == Crc32HashFunc::type()) {
    return makeRouteHandle<RouteHandleIf, HashRoute, Crc32HashFunc>(
      json, std::move(children));
  }
  if (funcType == WeightedCh3HashFunc::type()) {
    return makeRouteHandle<RouteHandleIf, HashRoute, WeightedCh3HashFunc>(
      json, std::move(children));
  }
  if (funcType == "Latest") {
    return makeRouteHandle<RouteHandleIf, LatestRoute>(
      json, std::move(children));
  }

  return nullptr;
}

template <class RouteHandleIf>
std::shared_ptr<RouteHandleIf>
RouteHandleProvider<RouteHandleIf>::makeHash(
    const folly::dynamic& json,
    std::vector<std::shared_ptr<RouteHandleIf>> children) {

  // Ch3 is default
  auto defaultFunc = Ch3HashFunc::type();
  folly::StringPiece funcType = defaultFunc;
  if (json.isObject() && json.count("hash_func")) {
    const auto& func = json["hash_func"];
    checkLogic(func.isString(), "HashRoute: hash_func is not string");
    funcType = func.stringPiece();
  }

  return createHash(funcType, json, std::move(children));
}

}}  // facebook::memcache
