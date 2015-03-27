/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "McrouterStackContext.h"

#include <glog/logging.h>

namespace facebook { namespace memcache { namespace mcrouter {

const char* const requestClassStr(RequestClass requestClass) {
  switch (requestClass) {
    case RequestClass::NORMAL:
      return "normal";
    case RequestClass::FAILOVER:
      return "failover";
    case RequestClass::SHADOW:
      return "shadow";
  }
  CHECK(false) << "Unknown request class";
}

}}}  // facebook::memcache::mcrouter
