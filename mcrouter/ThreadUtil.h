/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <folly/Range.h>

namespace facebook {
namespace memcache {

class McrouterOptions;

namespace mcrouter {

void mcrouterSetThisThreadName(
    const McrouterOptions& opts,
    folly::StringPiece prefix);
}
}
} // facebook::memcache::mcrouter
