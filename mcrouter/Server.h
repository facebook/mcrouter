/*
 *  Copyright (c) Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <functional>

namespace facebook {
namespace memcache {

class McrouterOptions;

namespace mcrouter {

class CarbonRouterInstanceBase;
class McrouterStandaloneOptions;

using StandalonePreRunCb =
    std::function<void(CarbonRouterInstanceBase& router)>;

/**
 * Spawns the standalone server and blocks until it's shutdown.
 *
 * @return True if server shut down cleanly, false if any errors occurred.
 */
template <class RouterInfo, template <class> class RequestHandler>
bool runServer(
    const McrouterOptions& mcrouterOpts,
    const McrouterStandaloneOptions& standaloneOpts,
    StandalonePreRunCb preRunCb = nullptr);

} // mcrouter
} // memcache
} // facebook

#include "Server-inl.h"
