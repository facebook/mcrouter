/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "OutstandingLimitRoute.h"

#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeOutstandingLimitRoute(
  McrouterRouteHandlePtr normalRoute,
  size_t maxOutstanding) {

  return std::make_shared<McrouterRouteHandle<OutstandingLimitRoute>>(
    std::move(normalRoute),
    maxOutstanding);
}

}}}  // facebook::memcache::mcrouter
