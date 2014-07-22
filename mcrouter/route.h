/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "folly/Range.h"
#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/fbi/nstring.h"

namespace facebook { namespace memcache { namespace mcrouter {

class proxy_t;
class ProxyGenericPool;
class ProxyPool;

enum proxy_send_stat_result_t {
  PROXY_SEND_OK = 0,
  PROXY_SEND_LOCAL_ERROR,
  PROXY_SEND_REMOTE_ERROR,
  PROXY_SEND_NUM_ERROR_TYPES
};

void update_send_stats(proxy_t *proxy, const McMsgRef& req,
                       proxy_send_stat_result_t res);

/**
 * True if pattern (like "/foo/a*c/") matches a route (like "/foo/abc")
 */
bool match_pattern_route(folly::StringPiece pattern,
                         folly::StringPiece route);

/**
 * Checks if the hash of routing part of the key is within a range
 * Used for probabilistic decisions, like stats sampling or shadowing.
 */
bool match_routing_key_hash(uint32_t routingKeyHash,
    double start_key_fraction,
    double end_key_fraction);

/* return the index of the server in pool, where request with key should be sent
   to. -1 is returned on error.
   NOTE: 1. if needed, hashtable contains salt already. */
int get_server_index_in_pool(const ProxyPool* pool,
                             const nstring_t hashable);

ProxyPool* pick_pool(ProxyGenericPool* generic_pool);

}}} // facebook::memcache::mcrouter
