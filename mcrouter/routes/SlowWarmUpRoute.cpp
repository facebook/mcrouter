/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "SlowWarmUpRoute.h"

#include <memory>

#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeSlowWarmUpRoute(
      McrouterRouteHandlePtr target,
      McrouterRouteHandlePtr failoverTarget,
      std::shared_ptr<SlowWarmUpRouteSettings> settings) {
  return makeMcrouterRouteHandle<SlowWarmUpRoute>(std::move(target),
                                                  std::move(failoverTarget),
                                                  std::move(settings));

}

}}} // facebook::memcache::mcrouter
