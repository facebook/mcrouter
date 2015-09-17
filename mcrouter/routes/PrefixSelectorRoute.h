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

#include <memory>

#include "mcrouter/lib/fbi/cpp/Trie.h"
#include "mcrouter/routes/McrouterRouteHandle.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

template <class RouteHandleIf>
class RouteHandleFactory;

namespace mcrouter {

/**
 * This class contains RouteHandles that should be used depending on key
 * prefix.
 */
class PrefixSelectorRoute {
 public:
  /// Trie that acts like map from key prefix to corresponding RouteHandle.
  Trie<std::shared_ptr<McrouterRouteHandleIf>> policies;
  /// Used when no RouteHandle found in policies
  std::shared_ptr<McrouterRouteHandleIf> wildcard;

  PrefixSelectorRoute() = default;

  PrefixSelectorRoute(RouteHandleFactory<McrouterRouteHandleIf>& factory,
                      const folly::dynamic& json);
};

}}}  // facebook::memcache::mcrouter
