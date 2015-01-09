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

struct mcrouter_exception : std::runtime_error {
  explicit mcrouter_exception(const std::string& e)
      : std::runtime_error(e) {}
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

mcrouter_t* mcrouter_new(const McrouterOptions& input_options,
                         bool spawnProxyThreads = true);
void mcrouter_free(mcrouter_t *router);

mcrouter_t* mcrouter_new_transient(const McrouterOptions& options);

const McrouterOptions& mcrouter_get_opts(mcrouter_t *router);

void free_all_libmcrouters();

}}} // facebook::memcache::mcrouter
