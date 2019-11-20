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
#include "mcrouter/ProxyDestinationBase.h"
#include "mcrouter/ProxyDestinationKey.h"
#include "mcrouter/lib/network/AccessPoint.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

template <class Transport>
std::shared_ptr<ProxyDestination<Transport>> ProxyDestinationMap::emplace(
    std::shared_ptr<AccessPoint> ap,
    std::chrono::milliseconds timeout,
    uint32_t qosClass,
    uint32_t qosPath) {
  std::shared_ptr<ProxyDestination<Transport>> destination;
  {
    std::lock_guard<std::mutex> lck(destinationsLock_);
    auto it = destinations_.find(ProxyDestinationKey(*ap, timeout));
    if (it != destinations_.end()) {
      destination = std::dynamic_pointer_cast<ProxyDestination<Transport>>(
          (*it)->selfPtr().lock());
      assert(destination); // if destination is in map, selfPtr should be OK
      return destination;
    }

    destination = ProxyDestination<Transport>::create(
        *proxy_, std::move(ap), timeout, qosClass, qosPath);
    destinations_.emplace(destination.get());
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
