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

#include <pthread.h>

#include <folly/Range.h>

namespace facebook { namespace memcache {

class McrouterOptions;

namespace mcrouter {

/*
 * Utility functions for launching threads and setting thread names.
 */
bool spawnThread(pthread_t* thread_handle, void** stack,
                 void* (thread_run)(void*), void* arg);

void mcrouterSetThreadName(pthread_t tid,
                           const McrouterOptions& opts,
                           folly::StringPiece prefix);

}}} // facebook::memcache::mcrouter
