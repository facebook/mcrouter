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

#include <string>

namespace facebook { namespace memcache {

class McrouterOptions;

namespace mcrouter {

/**
 * Get mcrotuer stats prefix.
 */
std::string getStatPrefix(const McrouterOptions& opts);

/**
 * Get the full path of the client debug fifo (i.e. debug fifo that replicates
 * AsyncMcClient network traffic)..
 *
 * @param opts    Mcrouter options.
 * @return        Full path of the fifo.
 */
std::string getClientDebugFifoFullPath(const McrouterOptions& opts);

/**
 * Get the full path of the server debug fifo (i.e. debug fifo that replicates
 * AsyncMcServer network traffic)..
 *
 * @param opts    Mcrouter options.
 * @return        Full path of the fifo.
 */
std::string getServerDebugFifoFullPath(const McrouterOptions& opts);

}}} // facebook::memcache::mcrouter
