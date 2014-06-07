/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

namespace facebook { namespace memcache { namespace mcrouter {

class McrouterStandaloneOptions;
struct mcrouter_t;

void runServer(const McrouterStandaloneOptions& standaloneOpts,
               mcrouter_t& router);

}  // facebook::memcache::mcrouter

}}  // facebook::memcache
