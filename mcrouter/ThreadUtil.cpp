/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#include "ThreadUtil.h"

#include <folly/Format.h>
#include <folly/system/ThreadName.h>

#include "mcrouter/options.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

void mcrouterSetThisThreadName(
    const McrouterOptions& opts,
    folly::StringPiece prefix) {
  auto name = folly::format("{}-{}", prefix, opts.router_name).str();
  if (!folly::setThreadName(name)) {
    LOG(WARNING) << "Unable to set thread name to " << name;
  }
}
}
}
} // facebook::memcache::mcrouter
