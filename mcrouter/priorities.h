/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

namespace facebook { namespace memcache {

class McrouterOptions;

namespace mcrouter {

class proxy_t;

enum event_priority_type_t {
  SERVER_REPLY=0,
  CLIENT_REPLY,
  CLIENT_REQUEST,
  SERVER_REQUEST,
  NUM_EVENT_PRIORITY_TYPES,
};

int get_event_priority(const McrouterOptions& opts, event_priority_type_t type);

void init_proxy_event_priorities(proxy_t* proxy);

}}} // facebook::memcache::mcrouter
