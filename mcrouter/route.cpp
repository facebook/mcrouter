/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "route.h"

#include <ctype.h>

#include <memory>

#include <folly/Range.h>

#include "mcrouter/async.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fbi/hash.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/proxy.h"
#include "mcrouter/stats.h"

using folly::StringPiece;

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
bool match_pattern_route(StringPiece pattern,
                         StringPiece route) {
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

#define STAT_UPDATE(CATEGORY)                                       \
  do {switch (op) {                                                 \
      case mc_op_get:                                               \
        stat_incr(proxy->stats, cmd_get_ ## CATEGORY, 1);           \
        break;                                                      \
      case mc_op_metaget:                                           \
        stat_incr(proxy->stats, cmd_meta_ ## CATEGORY, 1);          \
      case mc_op_add:                                               \
        stat_incr(proxy->stats, cmd_add_ ## CATEGORY, 1);           \
        break;                                                      \
      case mc_op_replace:                                           \
        stat_incr(proxy->stats, cmd_replace_ ## CATEGORY, 1);       \
        break;                                                      \
      case mc_op_set:                                               \
        stat_incr(proxy->stats, cmd_set_ ## CATEGORY, 1);           \
        break;                                                      \
      case mc_op_incr:                                              \
        stat_incr(proxy->stats, cmd_incr_ ## CATEGORY, 1);          \
        break;                                                      \
      case mc_op_decr:                                              \
        stat_incr(proxy->stats, cmd_decr_ ## CATEGORY, 1);          \
        break;                                                      \
      case mc_op_delete:                                            \
        stat_incr(proxy->stats, cmd_delete_ ## CATEGORY, 1);        \
        break;                                                      \
      case mc_op_lease_set:                                         \
        stat_incr(proxy->stats, cmd_lease_set_ ## CATEGORY, 1);     \
        break;                                                      \
      case mc_op_lease_get:                                         \
        stat_incr(proxy->stats, cmd_lease_get_ ## CATEGORY, 1);     \
        break;                                                      \
      default:                                                      \
        stat_incr(proxy->stats, cmd_other_ ## CATEGORY, 1);         \
        break;}} while(0)

void update_send_stats(proxy_t *proxy, mc_op_t op,
                       proxy_send_stat_result_t res) {

  switch (res) {
    case PROXY_SEND_OK:
      STAT_UPDATE(out_all_stat);
      STAT_UPDATE(out_all_count_stat);
      break;
    case PROXY_SEND_LOCAL_ERROR:
      STAT_UPDATE(local_error_stat);
      STAT_UPDATE(local_error_count_stat);
      break;
    case PROXY_SEND_REMOTE_ERROR:
      STAT_UPDATE(remote_error_stat);
      STAT_UPDATE(remote_error_count_stat);
      break;
    default:
      FBI_ASSERT(res < PROXY_SEND_NUM_ERROR_TYPES);
      break;
  }
}

}}} // facebook::memcache::mcrouter
