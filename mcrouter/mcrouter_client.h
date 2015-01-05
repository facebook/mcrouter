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

#include "mcrouter/lib/fbi/counting_sem.h"
#include "mcrouter/lib/fbi/queue.h"
#include "mcrouter/router.h"

class asox_queue_s;
using asox_queue_t = asox_queue_s*;

namespace facebook { namespace memcache { namespace mcrouter {

struct mcrouter_client_stats_t {
  uint32_t nreq;              // number of requests
  uint32_t op_count[mc_nops]; // number of requests for certain op
  uint32_t op_key_bytes[mc_nops]; // request key bytes
  uint32_t op_value_bytes[mc_nops]; // request + reply value bytes
  uint32_t ntmo;              // number of timeouts
  uint32_t nlocal_errors;     // number of local errors
  uint32_t nremote_errors;    // number of remote errors
};

struct mcrouter_client_t {
  mcrouter_t *router;

  mcrouter_client_callbacks_t callbacks;
  void *arg;

  proxy_t *proxy;

  mcrouter_client_stats_t stats;

  /// Maximum allowed requests in flight (unlimited if 0)
  unsigned int max_outstanding;
  counting_sem_t outstanding_reqs_sem;

  // whether the routing thread has received disconnect notification.
  int disconnected;

  // only updated by mcrouter thread, so we don't need any fancy atomic refcount
  int num_pending;

  TAILQ_ENTRY(mcrouter_client_t) entry;

  int _refcount;

  // If true implies that the underlying mcrouter has already been
  // freed. A zombie client can not serve any more requests.
  bool isZombie;

  /**
   * Automatically-assigned client id, used for QOS for different clients
   * sharing the same connection.
   */
  uint64_t clientId;

  mcrouter_client_t(
    mcrouter_t* router,
    mcrouter_client_callbacks_t callbacks,
    void *arg,
    size_t maximum_outstanding);

  mcrouter_client_t(const mcrouter_client_t&) = delete;
  mcrouter_client_t& operator=(const mcrouter_client_t&) = delete;
};

mcrouter_client_t* mcrouter_client_incref(mcrouter_client_t* client);
void mcrouter_client_decref(mcrouter_client_t* client);

typedef TAILQ_HEAD(, mcrouter_client_t) mcrouter_client_list_t;

}}} // facebook::memcache::mcrouter
