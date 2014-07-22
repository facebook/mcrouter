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

#include "folly/Range.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/lib/fbi/hash.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/async.h"
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

folly::StringPiece shard_number(const nstring_t& key) {
  char* id_begin = (char*)memchr(key.str, ':', key.len);
  if (id_begin == nullptr) {
    return "-1";
  }
  ++id_begin;
  char* id_end = (char*)memchr(id_begin, ':', key.len - (id_begin - key.str));
  if (id_end == nullptr) {
    return "-1";
  }
  if (!isdigit(*(id_end - 1))) {
    return "-1";
  }

  return folly::StringPiece(id_begin, id_end);
}

int shard_lookup(const ProxyPool* pool, const nstring_t key) {
  if (pool->shard_map.empty()) {
    return -1;
  }

  auto it = pool->shard_map.find(shard_number(key));
  if (it == pool->shard_map.end()) {
    return -1;
  }
  auto index = it->second;
  if (index >= pool->clients.size()) {
    return -1;
  }
  return index;
}

ProxyPool* pick_pool(ProxyGenericPool* generic_pool) {
  FBI_ASSERT(generic_pool);
  if (generic_pool->getType() != MIGRATED_POOL) {
    auto res = dynamic_cast<ProxyPool*>(generic_pool);
    FBI_ASSERT(res);
    return res;
  }

  time_t ts = time(nullptr);
  auto migrated_pool = dynamic_cast<ProxyMigratedPool*>(generic_pool);
  FBI_ASSERT(migrated_pool);
  if (ts < migrated_pool->migration_start_ts +
      migrated_pool->migration_interval_sec) {
    return migrated_pool->from_pool;
  } else {
    return migrated_pool->to_pool;
  }
}

static int ch3_lookup(const nstring_t hashable, size_t n) {
  if (n == 0) {
    LOG(ERROR) << "Empty pool";
    return -1;
  }

  if (n > furc_maximum_pool_size()) {
    LOG(ERROR) << "Pool size exceeds maximum";
    return -1;
  }

  return furc_hash(hashable.str, hashable.len, n);
}

/* doc in route.h */
int get_server_index_in_pool(const ProxyPool* pool,
                             const nstring_t hashable) {
  if (pool->clients.empty()) {
    LOG(ERROR) << "empty pool";
    return -1;
  }

  int n;
  switch(pool->hash) {
  case proxy_hash_crc32:
    n = (crc32_hash(hashable.str, hashable.len) & 0x7fffffff);
    n %= pool->clients.size();
    break;

  case proxy_hash_ch2:
    LOG(ERROR) << "continuum v2 not supported";
    return -1;

  case proxy_hash_shard:
    n = shard_lookup(pool, hashable);
    if (n < 0) {
      n = ch3_lookup(hashable, pool->clients.size());
    }
    break;

  case proxy_hash_const_shard:
    n = folly::to<int>(shard_number(hashable));
    if (n < 0 || n >= pool->clients.size()) {
      n = ch3_lookup(hashable, pool->clients.size());
    }
    break;

  case proxy_hash_ch3:
    n = ch3_lookup(hashable, pool->clients.size());
    break;

  case proxy_hash_wch3: {
    FBI_ASSERT(pool->wch3_func != nullptr);
    folly::StringPiece key(hashable.str, hashable.len);
    n = (*pool->wch3_func)(key);
    break;
  }

  default:
    // hash_latest needs more context, so is handled elsewhere
    FBI_ASSERT(pool->hash != proxy_hash_latest);
    return -1;
  }

  return n;
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
  do {switch (req->op) {                                            \
      case mc_op_get:                                               \
        stat_incr(proxy, cmd_get_ ## CATEGORY, 1);                  \
        break;                                                      \
      case mc_op_metaget:                                           \
        stat_incr(proxy, cmd_meta_ ## CATEGORY, 1);                 \
      case mc_op_add:                                               \
        stat_incr(proxy, cmd_add_ ## CATEGORY, 1);                  \
        break;                                                      \
      case mc_op_replace:                                           \
        stat_incr(proxy, cmd_replace_ ## CATEGORY, 1);              \
        break;                                                      \
      case mc_op_set:                                               \
        stat_incr(proxy, cmd_set_ ## CATEGORY, 1);                  \
        break;                                                      \
      case mc_op_incr:                                              \
        stat_incr(proxy, cmd_incr_ ## CATEGORY, 1);                 \
        break;                                                      \
      case mc_op_decr:                                              \
        stat_incr(proxy, cmd_decr_ ## CATEGORY, 1);                 \
        break;                                                      \
      case mc_op_delete:                                            \
        stat_incr(proxy, cmd_delete_ ## CATEGORY, 1);               \
        break;                                                      \
      case mc_op_lease_set:                                         \
        stat_incr(proxy, cmd_lease_set_ ## CATEGORY, 1);            \
        break;                                                      \
      case mc_op_lease_get:                                         \
        stat_incr(proxy, cmd_lease_get_ ## CATEGORY, 1);            \
        break;                                                      \
      default:                                                      \
        stat_incr(proxy, cmd_other_ ## CATEGORY, 1);                \
        break;}} while(0)

void update_send_stats(proxy_t *proxy, const McMsgRef& req,
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
