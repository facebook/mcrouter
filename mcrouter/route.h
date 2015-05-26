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

#include <folly/Range.h>

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * True if pattern (like "/foo/a*c/") matches a route (like "/foo/abc")
 */
bool match_pattern_route(folly::StringPiece pattern,
                         folly::StringPiece route);

}}} // facebook::memcache::mcrouter
