/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <mutex>
#include <stdexcept>

#include "mcrouter/ProxyBase.h"
#include "mcrouter/lib/network/AccessPoint.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class Transport>
std::shared_ptr<ProxyDestination<Transport>> ProxyDestinationMap::find(
    const AccessPoint& ap,
    std::chrono::milliseconds timeout) const {
  auto key = genProxyDestinationKey(ap, timeout);
  {
    std::lock_guard<std::mutex> lck(destinationsLock_);
    return std::dynamic_pointer_cast<ProxyDestination<Transport>>(find(key));
  }
}

template <class Transport>
std::shared_ptr<ProxyDestination<Transport>> ProxyDestinationMap::emplace(
    std::shared_ptr<AccessPoint> ap,
    std::chrono::milliseconds timeout,
    uint64_t qosClass,
    uint64_t qosPath,
    folly::StringPiece routerInfoName) {
  auto key = genProxyDestinationKey(*ap, timeout);
  auto destination = ProxyDestination<Transport>::create(
      *proxy_, std::move(ap), timeout, qosClass, qosPath, routerInfoName);
  {
    std::lock_guard<std::mutex> lck(destinationsLock_);
    auto destIt = destinations_.emplace(key, destination);
    destination->setKey(destIt.first->first);
  }

  // Update shared area of ProxyDestinations with same key from different
  // threads. This shared area is represented with TkoTracker class.
  proxy_->router().tkoTrackerMap().updateTracker(
      *destination, proxy_->router().opts().failures_until_tko);

  return destination;
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
