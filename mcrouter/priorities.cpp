/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "priorities.h"

#include "folly/io/async/EventBase.h"
#include "mcrouter/options.h"
#include "mcrouter/proxy.h"
/*
 * The purposes of this file are to
 *  A. ensure we don't set up more libevent priority levels than we need
 *  B. abstract away enabling/disabled priority levels
 */

namespace facebook { namespace memcache { namespace mcrouter {

static int NPRIORITIES = 4;
static int PRIORITIES[NUM_EVENT_PRIORITY_TYPES] =
  {0,  1,  2,  3};

int get_event_priority(const McrouterOptions& opts,
                       event_priority_type_t type) {
  if (!opts.use_priorities) {
    return -1;
  }

  int priority = PRIORITIES[type];

  FBI_ASSERT(priority >= 0);
  return priority;
}

void init_proxy_event_priorities(proxy_t* proxy) {
  if (proxy == nullptr || !proxy->opts.use_priorities) {
    return;
  }

  int ret = event_base_priority_init(proxy->eventBase->getLibeventBase(),
                                     NPRIORITIES);

  if (ret != 0) {
    LOG(ERROR) << "Failed to initialize event base priorities!";
  }
}

}}} // facebook::memcache::mcrouter
