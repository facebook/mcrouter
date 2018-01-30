/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <unordered_map>

#include <folly/Range.h>

#include "mcrouter/CarbonRouterInstance.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

bool getOptionsFromFlavor(
    folly::StringPiece flavorUri,
    std::unordered_map<std::string, std::string>&& optionOverrides,
    McrouterOptions& routerOptions);

} // detail

template <class RouterInfo>
CarbonRouterInstance<RouterInfo>* createRouterFromFlavor(
    folly::StringPiece persistenceId,
    folly::StringPiece flavorUri,
    std::unordered_map<std::string, std::string> optionOverrides) {
  if (auto router = CarbonRouterInstance<RouterInfo>::get(persistenceId)) {
    return router;
  }

  McrouterOptions options;
  if (!detail::getOptionsFromFlavor(
          flavorUri, std::move(optionOverrides), options)) {
    return nullptr;
  }

  return CarbonRouterInstance<RouterInfo>::init(persistenceId, options);
}

} // mcrouter
} // memcache
} // facebook
