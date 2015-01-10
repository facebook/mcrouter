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

#include <functional>

namespace facebook { namespace memcache { namespace mcrouter {

/* Parent cleanup function */
typedef std::function<void()> ParentCleanupFn;

/* Forks off child process and watches for its death if we're running in
   managed mode. */
void spawnManagedChild(ParentCleanupFn cleanupFn);

/* Shutdown parent and child process'.
 * Can only be called after spawnManagedChild(). */
bool shutdownFromChild();

}}} // namespace facebook::memcache::mcrouter
