/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Util.h"

#include <folly/Conv.h>

#include "mcrouter/lib/mc/msg.h"

namespace facebook { namespace memcache {

std::vector<std::string> describeFlags(uint64_t flags) {
  std::vector<std::string> out;

  uint64_t f = 1;
  while (flags > 0) {
    if (flags & f) {
      out.push_back(mc_flag_to_string(static_cast<enum mc_msg_flags_t>(f)));
      flags &= ~f;
    }
    f <<= 1;
  }

  return out;
}

}} // facebook::memcache
