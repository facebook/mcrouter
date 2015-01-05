/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "BigValueRoute.h"

#include "mcrouter/routes/McRouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

McrouterRouteHandlePtr makeBigValueRoute(McrouterRouteHandlePtr rh,
                                         BigValueRouteOptions options) {
  return makeMcrouterRouteHandle<BigValueRoute>(
    std::move(rh),
    std::move(options));
}

}}}
