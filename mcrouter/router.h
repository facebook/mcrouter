/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <stdexcept>
#include <unordered_map>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/stats.h"

namespace folly {
class EventBase;
}

/* API for libmcrouter */

namespace facebook { namespace memcache {

class McrouterOptions;

namespace mcrouter {

class mcrouter_t;
class mcrouter_client_t;

struct mcrouter_exception : std::runtime_error {
  explicit mcrouter_exception(const std::string& e)
      : std::runtime_error(e) {}
};

struct mcrouter_msg_t {
  mc_msg_t* req;
  McReply reply{mc_res_unknown};
  void *context;
  folly::Optional<McRequest> saved_request;
};

/**
 * mcrouter_init() will initialize a router handle if a router with the same
 * persistence_id hasn't been initialized. The new or existing router handle
 * is returned.
 *
 * mcrouter_get() is similar to mcrouter_init() except it returns null
 * if no router with the same persistence_id has been initialized
 *
 * mcrouter_free() clean up router handle (most likely still need some work)
 *
 * mcrouter_get_event_base() retrieves the event base of the next proxy to
 * be used.  Can be used in standalone mode to retrieve an event base to
 * run on with libmcrouter.
 */
mcrouter_t* mcrouter_init(const std::string& persistence_id,
                          const McrouterOptions& options);
mcrouter_t* mcrouter_get(const std::string& persistence_id);

mcrouter_t* mcrouter_new(const McrouterOptions& input_options);
void mcrouter_free(mcrouter_t *router);

mcrouter_t* mcrouter_new_transient(const McrouterOptions& options);

const McrouterOptions& mcrouter_get_opts(mcrouter_t *router);

typedef void (mcrouter_on_reply_t)(mcrouter_client_t *client,
                                   mcrouter_msg_t *router_req,
                                   void* client_context);

typedef void (mcrouter_on_cancel_t)(mcrouter_client_t *client,
                                    void* request_context,
                                    void* client_context);

typedef void (mcrouter_on_disconnect_ts_t)(void *client_context);

typedef void (mcrouter_on_request_sweep_t)(mcrouter_msg_t *router_req);

struct mcrouter_client_callbacks_t {
  mcrouter_on_reply_t* on_reply;
  mcrouter_on_cancel_t* on_cancel;
  mcrouter_on_disconnect_ts_t* on_disconnect;
};

/**
 * Create a handle to talk to mcrouter
 * The context will be passed back to the callbacks
 *
 * maximum_outstanding_requests: if nonzero, at most this many requests
 *   will be allowed to be in flight at any single point in time.
 *   mcrouter_send will block until the number of outstanding requests
 *   is less than this limit.
 *
 * The reference obtained through this call must be surrendered back to
 * mcrouter_client_disconnect().
 */
mcrouter_client_t *mcrouter_client_new(mcrouter_t *router,
                                       mcrouter_client_callbacks_t callbacks,
                                       void *context,
                                       size_t maximum_outstanding_requests,
                                       bool nonblocking);

// Mark client for cleanup.  This call releases the client reference.
void mcrouter_client_disconnect(mcrouter_client_t *client);

folly::EventBase* mcrouter_client_get_base(mcrouter_client_t *client);

void mcrouter_client_set_context(mcrouter_client_t* client, void* context);

std::unordered_map<std::string, int64_t> mcrouter_client_stats(
  mcrouter_client_t* client,
  bool clear);

int mcrouter_send(mcrouter_client_t *router_client,
                  const mcrouter_msg_t *requests, size_t nreqs);

int mcrouter_get_stats_age(mcrouter_t *router);

int mcrouter_get_stats(mcrouter_t *router,
                       int clear,
                       stat_group_t group,
                       stat_t **stats,
                       size_t *num_stats);

void mcrouter_free_stats(stat_t *stats,
                         size_t num_stats);

void free_all_libmcrouters();

}}} // facebook::memcache::mcrouter
