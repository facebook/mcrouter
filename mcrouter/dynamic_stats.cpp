/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
// for PRIu64 and friends in C++
#define __STDC_FORMAT_MACROS
#include "dynamic_stats.h"

#include <stdio.h>

#include <mutex>

#include "mcrouter/ProxyDestination.h"
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/lib/mc/msg.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

dynamic_stats_list_t all_dynamic_stats;
int dynamic_stats_initialized = 0;

inline const char*
proxy_client_state_to_string(const proxy_client_state_t state) {
  static const char* state_strings[] = {
    "unknown",
    "new",
    "up",
    "down",
    "tko",
  };

  return state_strings[state < PROXY_CLIENT_NUM_STATES
                      ? state
                      : PROXY_CLIENT_UNKNOWN];
}

}  // anonymous namespace

std::mutex& dynamic_stats_mutex() {
  static std::mutex m;
  return m;
}

void dynamic_stats_init() {
  std::lock_guard<std::mutex> guard(dynamic_stats_mutex());
  if (dynamic_stats_initialized == 0) {
    TAILQ_INIT(&all_dynamic_stats);
    dynamic_stats_initialized = 1;
  }
}

dynamic_stat_t* dynamic_stats_register(const stat_t* stat, void* ptr) {
  // Check for unique name?
  FBI_ASSERT(dynamic_stats_initialized);
  dynamic_stat_t* ret = (dynamic_stat_t*) malloc(sizeof(dynamic_stat_t));
  if (ret != nullptr) {
    memcpy(&ret->stat, stat, sizeof(stat_t));
    ret->entity_ptr = ptr;

    {
      std::lock_guard<std::mutex> guard(dynamic_stats_mutex());
      TAILQ_INSERT_TAIL(&all_dynamic_stats, ret, entry);
    }
  }
  return ret;
}

int dynamic_stats_unregister(dynamic_stat_t* stat_ptr) {
  FBI_ASSERT(stat_ptr);
  FBI_ASSERT(dynamic_stats_initialized);

  {
    std::lock_guard<std::mutex> guard(dynamic_stats_mutex());
    TAILQ_REMOVE(&all_dynamic_stats, stat_ptr, entry);
  }

  if (stat_ptr->stat.type == stat_string) {
    free(stat_ptr->stat.data.string);
  }
  free(stat_ptr);

  return 0;
}

dynamic_stats_list_t dynamic_stats_get_all() {
  FBI_ASSERT(dynamic_stats_initialized);
  return all_dynamic_stats;
}

#define SERVER_STATS_FORMAT "status:%s rtt_peak:%" PRIu64 " "
#define SERVER_STATS_RESULT_FORMAT "%s:%" PRIu64 " "
#define SERVER_STATS_REPL_FAIL_FORMAT "repl_fails:%" PRIu64 " "

std::string proxy_client_stat_to_str(void* ptr) {
  ProxyDestination* pdstn = (ProxyDestination*) ptr;

  uint64_t peak = 0;
  if (pdstn->stats.rtt_timer) {
    peak = fb_timer_get_recent_peak(pdstn->stats.rtt_timer);
  }

  std::string out = folly::stringPrintf(
    SERVER_STATS_FORMAT,
    proxy_client_state_to_string(pdstn->state()), peak);

  for (int ii = 0; ii < mc_nres; ii ++) {
    if (pdstn->stats.results[ii]) {
      out += folly::stringPrintf(
        SERVER_STATS_RESULT_FORMAT,
        mc_res_to_string((mc_res_t)ii) + sizeof("mc_res"),
        pdstn->stats.results[ii]);
    }
  }

  return out;
}

}}} // facebook::memcache::mcrouter
