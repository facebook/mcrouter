/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>
#include <utility>
#include <vector>

namespace facebook { namespace memcache { namespace mcrouter {

class McrouterRouteHandleIf;
class proxy_pool_shadowing_policy_t;

template <class RouteHandleIf>
using ShadowData = std::vector<std::pair<
          std::shared_ptr<RouteHandleIf>,
          std::shared_ptr<proxy_pool_shadowing_policy_t>>>;

using McrouterShadowData = ShadowData<McrouterRouteHandleIf>;

}}}  // facebook::memcache::mcrouter
