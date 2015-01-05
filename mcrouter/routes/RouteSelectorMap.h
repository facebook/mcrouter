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
#include <string>
#include <unordered_map>

namespace facebook { namespace memcache { namespace mcrouter {

class PrefixRouteSelector;

typedef std::unordered_map<std::string, std::shared_ptr<PrefixRouteSelector>>
  RouteSelectorMap;

}}}  // facebook::memcache::mcrouter
