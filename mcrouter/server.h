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

namespace facebook { namespace memcache {

class McrouterOptions;

namespace mcrouter {

class McrouterStandaloneOptions;

/**
 * Spawns the standalone server and blocks until it's shutdown.
 *
 * @return True if server shut down cleanly, false if any errors occurred.
 */
bool runServer(const McrouterStandaloneOptions& standaloneOpts,
               const McrouterOptions& mcrouterOpts);

}  // facebook::memcache::mcrouter

}}  // facebook::memcache
