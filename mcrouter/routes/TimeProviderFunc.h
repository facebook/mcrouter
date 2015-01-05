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

#include "mcrouter/config.h"

namespace facebook { namespace memcache { namespace mcrouter {

/* Time Provider Func for Migrate route */
class TimeProviderFunc {
 public:
  template <class Request>
  time_t operator() (const Request& req) const {
    return nowWallSec();
  }
};

}}}  // facebook::memcache::mcrouter
