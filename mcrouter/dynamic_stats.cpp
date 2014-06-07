// for PRIu64 and friends in C++
#define __STDC_FORMAT_MACROS

#include "dynamic_stats.h"

#include <limits>
#include <mutex>

#include <stdio.h>
#include "mcrouter/lib/fbi/timer.h"
#include "mcrouter/lib/mc/msg.h"

#include "mcrouter/ProxyDestination.h"

#ifndef UINT64_MAX
static const uint64_t UINT64_MAX = std::numeric_limits<uint64_t>::max();
#endif

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

dynamic_stats_list_t all_dynamic_stats;
int num_dynamic_stats = 0;
int dynamic_stats_initialized = 0;
std::mutex dynamic_stats_mutex;


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

void dynamic_stats_init() {
  std::lock_guard<std::mutex> guard(dynamic_stats_mutex);
  if (dynamic_stats_initialized == 0) {
    TAILQ_INIT(&all_dynamic_stats);
    num_dynamic_stats = 0;
    dynamic_stats_initialized = 1;
  }
}

void dynamic_stats_lock(){
  dynamic_stats_mutex.lock();
}

void dynamic_stats_unlock(){
  dynamic_stats_mutex.unlock();
}

dynamic_stat_t* dynamic_stats_register(const stat_t* stat, void* ptr) {
  // Check for unique name?
  FBI_ASSERT(dynamic_stats_initialized);
  dynamic_stat_t* ret = (dynamic_stat_t*) malloc(sizeof(dynamic_stat_t));
  if (ret != nullptr) {
    memcpy(&ret->stat, stat, sizeof(stat_t));
    ret->entity_ptr = ptr;

    {
      std::lock_guard<std::mutex> guard(dynamic_stats_mutex);
      TAILQ_INSERT_TAIL(&all_dynamic_stats, ret, entry);
      ++num_dynamic_stats;
    }
  }
  return ret;
}

int dynamic_stats_unregister(dynamic_stat_t* stat_ptr) {
  FBI_ASSERT(stat_ptr);
  FBI_ASSERT(dynamic_stats_initialized);

  {
    std::lock_guard<std::mutex> guard(dynamic_stats_mutex);
    TAILQ_REMOVE(&all_dynamic_stats, stat_ptr, entry);
    --num_dynamic_stats;
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

int dynamic_stats_get_num_stats() {
  return num_dynamic_stats;
}

#define SERVER_STATS_FORMAT "status:%s rtt_peak:%" PRIu64 " "
#define SERVER_STATS_RESULT_FORMAT "%s:%" PRIu64 " "
#define SERVER_STATS_REPL_FAIL_FORMAT "repl_fails:%" PRIu64 " "

size_t dynamic_stat_size() {
  static size_t size = 0;
  if (!size) {

    for (int ii = 0; ii < mc_nres; ii ++) {
      size += snprintf(
          0, 0,
          SERVER_STATS_RESULT_FORMAT,
          mc_res_to_string((mc_res_t)ii) + sizeof("mc_res"),
          UINT64_MAX);
    }

    proxy_client_state_t longest_status = (proxy_client_state_t)0;
    size_t longest_status_len = 0;
    for (int ii = 0; ii < PROXY_CLIENT_NUM_STATES; ii ++) {
      size_t len = strlen(proxy_client_state_to_string(
                            (proxy_client_state_t)ii));
      if (len > longest_status_len) {
        longest_status_len = len;
        longest_status = (proxy_client_state_t)ii;
      }
    }

    size += snprintf(
        0, 0,
        SERVER_STATS_FORMAT,
        proxy_client_state_to_string(longest_status),
        UINT64_MAX);

    size += snprintf(
      0, 0,
      SERVER_STATS_REPL_FAIL_FORMAT,
      UINT64_MAX);
  }
  return size;
}

size_t proxy_client_stat_to_str(char *buf,
                                size_t max,
                                void* ptr) {

  ProxyDestination* pdstn = (ProxyDestination*) ptr;

  size_t sz = 0;

  uint64_t peak = 0;
  if (pdstn->stats.rtt_timer) {
    peak = fb_timer_get_recent_peak(pdstn->stats.rtt_timer);
  }

  sz += snprintf(
      buf + sz, max - sz,
      SERVER_STATS_FORMAT,
      proxy_client_state_to_string(pdstn->state()), peak);

  for (int ii = 0; ii < mc_nres; ii ++) {
    if (pdstn->stats.results[ii]) {
      sz += snprintf(
          buf + sz, max - sz,
          SERVER_STATS_RESULT_FORMAT,
          mc_res_to_string((mc_res_t)ii) + sizeof("mc_res"),
          pdstn->stats.results[ii]);
    }
  }

  return sz;
}

}}} // facebook::memcache::mcrouter
