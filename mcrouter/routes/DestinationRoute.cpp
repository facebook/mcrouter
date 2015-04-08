/*
 *  Copyright (c) 2015, Facebook, Inc.
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

std::string DestinationRoute::routeName() const {
  return folly::sformat("host|pool={}|id={}|ssl={}|ap={}|timeout={}ms",
    client_->pool.getName(),
    client_->indexInPool,
    client_->useSsl,
    client_->ap.toString(),
    client_->server_timeout.count());
}

McrouterRouteHandlePtr makeDestinationRoute(
  std::shared_ptr<const ProxyClientCommon> client,
  std::shared_ptr<ProxyDestination> destination) {

  return std::make_shared<McrouterRouteHandle<DestinationRoute>>(
    std::move(client),
    std::move(destination));
}

}}}
