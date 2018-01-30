/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>
#include <unordered_map>

#include <folly/Range.h>

#include "mcrouter/lib/network/gen/Memcache.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

// make sure MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND can be exactly divided by
// MOVING_AVERAGE_BIN_SIZE_IN_SECOND
// the window size within which average stat rate is calculated
#define MOVING_AVERAGE_WINDOW_SIZE_IN_SECOND (60 * 4)

// the bin size for average stat rate
#define MOVING_AVERAGE_BIN_SIZE_IN_SECOND (1)

// define stat_name_t
#define STAT(name, ...) name##_stat,
#define STUI STAT
#define STUIR STAT
#define STSI STAT
#define STSS STAT
enum stat_name_t {
#include "stat_list.h"
  num_stats,
};
#undef STAT
#undef STUI
#undef STUIR
#undef STSI
#undef STSS

// Forward declarations
class CarbonRouterInstanceBase;
class ProxyBase;

/** statistics ftw */

struct stat_s;
typedef std::string (*string_fn_t)(void*);

enum stat_type_t {
  stat_string,
  stat_uint64,
  stat_int64,
  stat_double,
  //  stat_percentile, // TBD
  num_stat_types
};

enum stat_group_t {
  mcproxy_stats = 0x1,
  detailed_stats = 0x2,
  cmd_error_stats = 0x20,
  ods_stats = 0x40,
  rate_stats = 0x100,
  count_stats = 0x200,
  max_stats = 0x800,
  max_max_stats = 0x1000,
  all_stats = 0xffff,
  server_stats = 0x10000,
  suspect_server_stats = 0x40000,
  unknown_stats = 0x10000000,
};

/** defines a statistic: name, type, and data */
struct stat_t {
  folly::StringPiece name;
  int group;
  stat_type_t type;
  int aggregate;
  union {
    char* string;
    uint64_t uint64;
    int64_t int64;
    double dbl;
    void* pointer;
  } data;
};

void init_stats(stat_t* stats);

inline void stat_incr(stat_t* stats, stat_name_t stat_num, int64_t amount) {
  stats[stat_num].data.uint64 += amount;
}

inline void stat_decr(stat_t* stats, stat_name_t stat_num, int64_t amount) {
  stat_incr(stats, stat_num, -amount);
}

void stat_incr_safe(stat_t*, stat_name_t, int64_t amount = 1);
void stat_decr_safe(stat_t*, stat_name_t);

/**
 * Current aggregation of rate of stats[idx] (which must be an aggregated
 * rate stat), units will be per second.
 */
double stats_aggregate_rate_value(
    const CarbonRouterInstanceBase& router,
    int idx);

/**
 * Current max between all buckets of stats[idx] (which must be an aggregated
 * max stat)
 */
uint64_t stats_aggregate_max_value(
    const CarbonRouterInstanceBase& router,
    int idx);

/**
 * Current max between all proxies amongst all buckets of stats[idx]
 */
uint64_t stats_aggregate_max_max_value(
    const CarbonRouterInstanceBase& router,
    int idx);

void stat_set_uint64(stat_t*, stat_name_t, uint64_t);
uint64_t stat_get_uint64(const stat_t*, stat_name_t);
uint64_t stat_get_config_age(const stat_t* stats, uint64_t now);
McStatsReply stats_reply(ProxyBase*, folly::StringPiece);
void prepare_stats(CarbonRouterInstanceBase& router, stat_t* stats);

void set_standalone_args(folly::StringPiece args);

} // mcrouter
} // memcache
} // facebook
