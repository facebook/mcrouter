/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/lib/fbi/queue.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

// Might want to have a linked list of linked lists to handle other classes
// of dynamic stats
struct dynamic_stat_t {
  stat_t stat;
  void* entity_ptr;
  TAILQ_ENTRY(dynamic_stat_t) entry;
};

typedef TAILQ_HEAD(dynamic_stats_list, dynamic_stat_t) dynamic_stats_list_t;

void dynamic_stats_init();
void dynamic_stats_lock();
void dynamic_stats_unlock();
dynamic_stat_t* dynamic_stats_register(const stat_t*, void*);
int dynamic_stats_unregister(dynamic_stat_t*);
dynamic_stats_list_t dynamic_stats_get_all();
int dynamic_stats_get_num_stats();
size_t dynamic_stat_size();
size_t proxy_client_stat_to_str(char *buf,
                                size_t size,
                                void* ptr);

}}} // facebook::memcache::mcrouter
