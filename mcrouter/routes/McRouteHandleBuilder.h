/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <template <typename... Ignored> class R,
          typename... RArgs,
          typename... Args>
McrouterRouteHandlePtr makeMcrouterRouteHandle(Args&&... args) {
  return makeRouteHandle<McrouterRouteHandleIf, R, RArgs...>(
      std::forward<Args>(args)...
    );
}

}}}
