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

#include <folly/experimental/StringKeyedUnorderedMap.h>

namespace facebook { namespace memcache { namespace mcrouter {

class PrefixSelectorRoute;

using RouteSelectorMap =
  folly::StringKeyedUnorderedMap<std::shared_ptr<PrefixSelectorRoute>>;

}}}  // facebook::memcache::mcrouter
