/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

namespace facebook {
namespace memcache {

class McrouterOptions;

namespace mcrouter {

class McrouterStandaloneOptions;

/**
 * Spawns the standalone server and blocks until it's shutdown.
 *
 * @return True if server shut down cleanly, false if any errors occurred.
 */
template <class RouterInfo, template <class> class RequestHandler>
bool runServer(
    const McrouterStandaloneOptions& standaloneOpts,
    const McrouterOptions& mcrouterOpts);

} // mcrouter
} // memcache
} // facebook

#include "Server-inl.h"
