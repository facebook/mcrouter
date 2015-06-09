/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/dynamic.h>

#include "mcrouter/lib/Ch3HashFunc.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/Crc32HashFunc.h"
#include "mcrouter/lib/routes/HashRoute.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/ShardHashFunc.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeNullRoute();

McrouterRouteHandlePtr makeLatestRoute(
  const folly::dynamic& json,
  std::vector<McrouterRouteHandlePtr> targets);

McrouterRouteHandlePtr makeHashRouteCrc32(
  std::vector<McrouterRouteHandlePtr> rh,
  std::string salt) {

  auto n = rh.size();
  if (n == 0) {
    return makeNullRoute();
  }
  if (n == 1) {
    return std::move(rh[0]);
  }

  return makeMcrouterRouteHandle<HashRoute, Crc32HashFunc>(
    std::move(rh),
    std::move(salt),
    Crc32HashFunc(n));
}

McrouterRouteHandlePtr makeHashRouteCh3(
  std::vector<McrouterRouteHandlePtr> rh,
  std::string salt) {

  auto n = rh.size();
  if (n == 0) {
    return makeNullRoute();
  }
  if (n == 1) {
    return std::move(rh[0]);
  }
  return makeMcrouterRouteHandle<HashRoute, Ch3HashFunc>(
    std::move(rh),
    std::move(salt),
    Ch3HashFunc(n));
}

McrouterRouteHandlePtr makeHashRouteConstShard(
  std::vector<McrouterRouteHandlePtr> rh,
  std::string salt) {

  auto n = rh.size();
  if (n == 0) {
    return makeNullRoute();
  }
  if (n == 1) {
    return std::move(rh[0]);
  }

  return makeMcrouterRouteHandle<HashRoute, ConstShardHashFunc>(
    std::move(rh),
    std::move(salt),
    ConstShardHashFunc(n));
}

McrouterRouteHandlePtr makeHashRouteWeightedCh3(
  std::vector<McrouterRouteHandlePtr> rh,
  std::string salt,
  WeightedCh3HashFunc func) {

  auto n = rh.size();
  if (n == 0) {
    return makeNullRoute();
  }
  if (n == 1) {
    return std::move(rh[0]);
  }

  return makeMcrouterRouteHandle<HashRoute, WeightedCh3HashFunc>(
    std::move(rh),
    std::move(salt),
    std::move(func));
}

McrouterRouteHandlePtr makeHashRoute(
  const folly::dynamic& json,
  std::vector<McrouterRouteHandlePtr> rh) {

  std::string salt;
  folly::StringPiece funcType = Ch3HashFunc::type();
  if (json.isObject()) {
    if (auto jsalt = json.get_ptr("salt")) {
      checkLogic(jsalt->isString(), "HashRoute: salt is not a string");
      salt = jsalt->stringPiece().str();
    }
    if (auto jhashFunc = json.get_ptr("hash_func")) {
      checkLogic(jhashFunc->isString(),
                 "HashRoute: hash_func is not a string");
      funcType = jhashFunc->stringPiece();
    }
  }

  if (funcType == Ch3HashFunc::type()) {
    return makeHashRouteCh3(std::move(rh), std::move(salt));
  } else if (funcType == Crc32HashFunc::type()) {
    return makeHashRouteCrc32(std::move(rh), std::move(salt));
  } else if (funcType == WeightedCh3HashFunc::type()) {
    WeightedCh3HashFunc func{json, rh.size()};
    return makeHashRouteWeightedCh3(std::move(rh), std::move(salt),
                                    std::move(func));
  } else if (funcType == ConstShardHashFunc::type()) {
    return makeHashRouteConstShard(std::move(rh), std::move(salt));
  } else if (funcType == "Latest") {
    return makeLatestRoute(json, std::move(rh));
  }
  checkLogic(false, "Unknown hash function: {}", funcType);
  return nullptr;
}

McrouterRouteHandlePtr makeHashRoute(
    RouteHandleFactory<McrouterRouteHandleIf>& factory,
    const folly::dynamic& json) {

  std::vector<McrouterRouteHandlePtr> children;
  if (json.isObject()) {
    if (auto jchildren = json.get_ptr("children")) {
      children = factory.createList(*jchildren);
    }
  } else {
    children = factory.createList(json);
  }
  return makeHashRoute(json, std::move(children));
}

}}}  // facebook::memcache::mcrouter
