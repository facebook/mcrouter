/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/routes/ProxyRoute.h"

#include "mcrouter/McrouterInstance.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/RootRoute.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

McrouterRouteHandlePtr makeBigValueRoute(McrouterRouteHandlePtr ch,
                                         BigValueRouteOptions options);

McrouterRouteHandlePtr
makeDestinationRoute(std::shared_ptr<const ProxyClientCommon> client,
                     std::shared_ptr<ProxyDestination> destination);

McrouterRouteHandlePtr makeLoggingRoute(McrouterRouteHandlePtr rh);

ProxyRoute::ProxyRoute(proxy_t *proxy, const RouteSelectorMap &routeSelectors)
    : proxy_(proxy),
      root_(std::make_shared<McrouterRouteHandle<RootRoute>>(
          proxy_, routeSelectors)) {
  if (proxy_->getRouterOptions().big_value_split_threshold != 0) {
    BigValueRouteOptions options(
        proxy_->getRouterOptions().big_value_split_threshold,
        proxy_->getRouterOptions().big_value_batch_size);
    root_ = makeBigValueRoute(std::move(root_), std::move(options));
  }
  if (proxy_->getRouterOptions().enable_logging_route) {
    root_ = makeLoggingRoute(std::move(root_));
  }
}

std::vector<McrouterRouteHandlePtr> ProxyRoute::getAllDestinations() const {
  std::vector<McrouterRouteHandlePtr> rh;
  auto clients = proxy_->getConfig()->getClients();
  for (auto &client : clients) {
    auto dest = proxy_->destinationMap->fetch(*client);
    rh.push_back(makeDestinationRoute(std::move(client), std::move(dest)));
  }
  return rh;
}

bool ProxyRoute::queryLeaseTokenMap(uint64_t leaseToken,
    uint64_t& originalLeaseToken,
    std::shared_ptr<ProxyDestination>& destination,
    std::chrono::milliseconds& timeout) const {

  if (auto leaseTokenMap = proxy_->router().leaseTokenMap()) {
    std::shared_ptr<const AccessPoint> ap;
    if (!leaseTokenMap->query(leaseToken, originalLeaseToken,
                              ap, timeout)) {
      return false;
    }

    destination = proxy_->destinationMap->find(*ap, timeout);
    return destination != nullptr;
  }

  return false;
}

}
}
} // facebook::memcache::mcrouter
