/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McrouterFiberContext.h"

namespace facebook { namespace memcache { namespace mcrouter {

const RequestClass RequestClass::kFailover{0x1};
const RequestClass RequestClass::kShadow{0x2};

const char* RequestClass::toString() const {
  if (is(RequestClass::kFailover) && is(RequestClass::kShadow)) {
    return "failover|shadow";
  } else if (is(RequestClass::kFailover)) {
    return "failover";
  } else if (is(RequestClass::kShadow)) {
    return "shadow";
  }
  return "normal";
}

}}}  // facebook::memcache::mcrouter
