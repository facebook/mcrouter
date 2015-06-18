/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ThreadUtil.h"

#include <folly/Format.h>
#include <folly/ThreadName.h>

#include "mcrouter/options.h"

namespace facebook { namespace memcache { namespace mcrouter {

void mcrouterSetThisThreadName(const McrouterOptions& opts,
                               folly::StringPiece prefix) {
  auto name = folly::format("{}-{}", prefix, opts.router_name).str();
  if (!folly::setThreadName(name)) {
    LOG(WARNING) << "Unable to set thread name to " << name;
  }
}

}}} // facebook::memcache::mcrouter
