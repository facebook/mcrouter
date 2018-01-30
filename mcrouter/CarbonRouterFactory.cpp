/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "CarbonRouterFactory.h"

#include <unordered_map>

#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/config.h"

namespace facebook {
namespace memcache {
namespace mcrouter {
namespace detail {

bool getOptionsFromFlavor(
    folly::StringPiece flavorUri,
    std::unordered_map<std::string, std::string>&& optionOverrides,
    McrouterOptions& routerOptions) {
  std::unordered_map<std::string, std::string> optionsMap;
  if (!readLibmcrouterFlavor(flavorUri, optionsMap)) {
    return false;
  }

  for (auto& it : optionOverrides) {
    optionsMap[it.first] = std::move(it.second);
  }

  auto errors = routerOptions.updateFromDict(optionsMap);
  for (const auto& err : errors) {
    MC_LOG_FAILURE(
        routerOptions,
        failure::Category::kInvalidOption,
        "Option parse error: {}={}, {}",
        err.requestedName,
        err.requestedValue,
        err.errorMsg);
  }
  return true;
}

} // detail
} // mcrouter
} // memcache
} // facebook
