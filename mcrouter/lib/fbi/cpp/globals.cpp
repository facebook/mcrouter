/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "globals.h"

#include <unistd.h>

#include <folly/IPAddress.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"
#include "mcrouter/lib/fbi/network.h"

namespace facebook { namespace memcache { namespace globals {

namespace {

bool getAddrHash(const sockaddr *addr, void *ctx) {
  try {
    folly::IPAddress ip(addr);
    if (!ip.isLoopback() && !ip.isLinkLocal()) {
      auto result = (uint32_t*) ctx;
      result[0] = 1;
      result[1] = ip.hash();
      return false;
    }
  } catch (...) {
    return true;
  }

  return true;
}

// get hash from the first non-loopback/non-linklocal ip4/6 address
uint32_t getHash() {
  uint32_t result[2] = {0};

  if (!for_each_localaddr(getAddrHash, result)) {
    LOG_FAILURE("mcrouter", failure::Category::kSystemError,
                "Can not enumerate local addresses: {}", strerror(errno));
    return 0;
  }

  if (result[0] == 0) {
    LOG_FAILURE("mcrouter", failure::Category::kBadEnvironment,
                "Can not find a valid ip addresss");
    return 0;
  }

  return result[1];
}

}  // anonymous namespace

uint32_t hostid() {
  static uint32_t h = getHash();
  return h;
}

}}} // facebook::memcache
