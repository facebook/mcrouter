/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/ShardHashFunc.h"
#include "mcrouter/lib/Ch3HashFunc.h"
#include "mcrouter/lib/Crc32HashFunc.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/lib/routes/HashRoute.h"

namespace facebook { namespace memcache {

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, HashRoute, Ch3HashFunc>(
  const folly::dynamic&,
  std::vector<std::shared_ptr<mcrouter::McrouterRouteHandleIf>>&&);

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, HashRoute, Crc32HashFunc>(
  const folly::dynamic&,
  std::vector<std::shared_ptr<mcrouter::McrouterRouteHandleIf>>&&);

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, HashRoute,
                WeightedCh3HashFunc>(
  const folly::dynamic&,
  std::vector<std::shared_ptr<mcrouter::McrouterRouteHandleIf>>&&);

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, HashRoute,
                mcrouter::ConstShardHashFunc>(
  const folly::dynamic&,
  std::vector<std::shared_ptr<mcrouter::McrouterRouteHandleIf>>&&);

template std::shared_ptr<mcrouter::McrouterRouteHandleIf>
makeRouteHandle<mcrouter::McrouterRouteHandleIf, HashRoute,
                mcrouter::ShardHashFunc>(
  const folly::dynamic&,
  std::vector<std::shared_ptr<mcrouter::McrouterRouteHandleIf>>&&);

namespace mcrouter {

McrouterRouteHandlePtr makeHashRouteCrc32(
  std::string name,
  std::vector<McrouterRouteHandlePtr> rh,
  const std::string& salt) {

  auto n = rh.size();
  return makeMcrouterRouteHandle<HashRoute, Crc32HashFunc>(
    std::move(name),
    std::move(rh),
    salt,
    Crc32HashFunc(n));
}

McrouterRouteHandlePtr makeHashRouteCh3(
  std::string name,
  std::vector<McrouterRouteHandlePtr> rh,
  const std::string& salt) {

  auto n = rh.size();
  return makeMcrouterRouteHandle<HashRoute, Ch3HashFunc>(
    std::move(name),
    std::move(rh),
    salt,
    Ch3HashFunc(n));
}

McrouterRouteHandlePtr makeHashRouteShard(
  std::string name,
  std::vector<McrouterRouteHandlePtr> rh,
  const std::string& salt,
  StringKeyedUnorderedMap<size_t> shard_map) {

  auto n = rh.size();
  return makeMcrouterRouteHandle<HashRoute, ShardHashFunc>(
    std::move(name),
    std::move(rh),
    salt,
    ShardHashFunc(std::move(shard_map), n));
}

McrouterRouteHandlePtr makeHashRouteConstShard(
  std::string name,
  std::vector<McrouterRouteHandlePtr> rh,
  const std::string& salt) {

  auto n = rh.size();
  return makeMcrouterRouteHandle<HashRoute, ConstShardHashFunc>(
    std::move(name),
    std::move(rh),
    salt,
    ConstShardHashFunc(n));
}

McrouterRouteHandlePtr makeHashRouteWeightedCh3(
  std::string name,
  std::vector<McrouterRouteHandlePtr> rh,
  const std::string& salt,
  WeightedCh3HashFunc& func) {

  return makeMcrouterRouteHandle<HashRoute, WeightedCh3HashFunc>(
    std::move(name),
    std::move(rh),
    salt,
    func);
}


}}}
