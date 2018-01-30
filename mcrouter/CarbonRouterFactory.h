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

/**
 * Creates a carbon router from a flavor file.
 *
 * @param persistenceId   Persistence ID of your carbon router instance.
 *                        If an instance with the given persistenceId already
 *                        exists, returns a pointer to it.
 *                        Otherwise, spins up a new CarbonRouterInstance.
 * @param flavorUri       URI of the flavor containing the router options.
 *                        The flavor URI has to be prefixed by it's provider
 *                        (e.g. if it's a file, it should be: "file:<PATH>").
 * @param overrides       Optional params containing any option that should
 *                        override the option provided by the flavor.
 *
 * @return                A pointer to CarbonRouterInstance.
 *                        May return nullptr if config is invalid or if
 *                        CarbonRouterManager singleton is unavailable.
 */
template <class RouterInfo>
CarbonRouterInstance<RouterInfo>* createRouterFromFlavor(
    folly::StringPiece persistenceId,
    folly::StringPiece flavorUri,
    std::unordered_map<std::string, std::string> optionOverrides =
        std::unordered_map<std::string, std::string>());

} // mcrouter
} // memcache
} // facebook

#include "CarbonRouterFactory-inl.h"
