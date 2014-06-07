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
#include <string>
#include <unordered_map>

#include "mcrouter/routes/McrouterRouteHandle.h"

namespace facebook { namespace memcache { namespace mcrouter {

class PrefixRouteSelector;

struct RouteSelectorMap {
  std::unordered_map<std::string, std::shared_ptr<PrefixRouteSelector>> routes;
  std::unordered_map<std::string, McrouterRouteHandlePtr> pools;
};

}}}  // facebook::memcache::mcrouter
