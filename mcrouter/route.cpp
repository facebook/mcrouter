/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "route.h"

#include <ctype.h>

#include <memory>

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/hash.h"

namespace facebook { namespace memcache { namespace mcrouter {

static bool match_pattern_helper(const char* pi,
                                 const char* pend,
                                 const char* ri,
                                 const char* rend) {
  while (pi != pend) {
    if (ri == rend) {
      return false;
    } else if (*pi == '*') {
      /* Swallow multiple stars */
      do {
        ++pi;
      } while (pi != pend && *pi == '*');

      if (pi == pend) {
        /* Terminating star matches any string not containing slashes */
        return memchr(ri, '/', rend - ri) == nullptr;
      } else if (*pi == '/') {
        /* start + slash: advance ri to the next slash */
        ri = static_cast<const char*>(memchr(ri, '/', rend - ri));
        if (ri == nullptr) {
          return false;
        }
      } else {
        /* Try to match '*' with every prefix,
           recursively try to match the remainder */
        while (ri != rend && *ri != '/') {
          if (match_pattern_helper(pi, pend, ri, rend)) {
            return true;
          }
          ++ri;
        }
        return false;
      }
    } else if (*pi++ != *ri++) {
      return false;
    }
  }

  return ri == rend;
}

/**
 * True if pattern (like "/foo/a*c/") matches a route (like "/foo/abc")
 */
bool match_pattern_route(folly::StringPiece pattern,
                         folly::StringPiece route) {
  return match_pattern_helper(pattern.begin(), pattern.end(),
                              route.begin(), route.end());
}

bool match_routing_key_hash(uint32_t routingKeyHash,
    double start_key_fraction,
    double end_key_fraction) {
  const uint32_t m = std::numeric_limits<uint32_t>::max();

  start_key_fraction = std::max(0.0, start_key_fraction);
  end_key_fraction = std::min(1.0, end_key_fraction);

  uint32_t keyStart = start_key_fraction * m;
  uint32_t keyEnd = end_key_fraction * m;

  /* Make sure that hash is always < max(), so [0.0, 1.0) really includes
     everything */
  auto keyHash = routingKeyHash % (m - 1);

  return keyHash >= keyStart && keyHash < keyEnd;
}

}}} // facebook::memcache::mcrouter
