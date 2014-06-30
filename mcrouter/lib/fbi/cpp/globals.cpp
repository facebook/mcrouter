/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "globals.h"

#include <unistd.h>

namespace facebook { namespace memcache { namespace globals {

uint32_t hostid() {
  static uint32_t h = gethostid();
  return h;
}

}}} // facebook::memcache
