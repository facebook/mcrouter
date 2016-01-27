/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "DestinationRoute.h"

#include <folly/Format.h>

namespace facebook { namespace memcache { namespace mcrouter {

const char* const kFailoverTag = ":failover=1";

std::string DestinationRoute::routeName() const {
  return folly::sformat("host|pool={}|id={}|ap={}|timeout={}ms",
    poolName_,
    indexInPool_,
    destination_->accessPoint()->toString(),
    timeout_.count());
}

std::string DestinationRoute::keyWithFailoverTag(
    folly::StringPiece fullKey) const {
  return folly::to<std::string>(fullKey, kFailoverTag);
}

McrouterRouteHandlePtr makeDestinationRoute(
  std::shared_ptr<ProxyDestination> destination,
  std::string poolName,
  size_t indexInPool,
  std::chrono::milliseconds timeout,
  bool keepRoutingPrefix) {

  return std::make_shared<McrouterRouteHandle<DestinationRoute>>(
    std::move(destination),
    std::move(poolName),
    indexInPool,
    timeout,
    keepRoutingPrefix);
}

}}}  // facebook::memcache::mcrouter
