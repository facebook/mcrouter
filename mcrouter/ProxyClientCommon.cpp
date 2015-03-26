/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyClientCommon.h"

#include <folly/Format.h>

#include "mcrouter/ClientPool.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyClientCommon::ProxyClientCommon(const ClientPool& pool_,
                                     std::chrono::milliseconds timeout,
                                     AccessPoint ap_,
                                     int keep_routing_prefix_,
                                     bool useSsl_,
                                     uint64_t qos_)
    : pool(pool_),
      ap(std::move(ap_)),
      keep_routing_prefix(keep_routing_prefix_),
      server_timeout(std::move(timeout)),
      indexInPool(pool.getClients().size()),
      useSsl(useSsl_),
      qos(qos_) {
}

std::string ProxyClientCommon::genProxyDestinationKey(
    bool include_timeout) const {
  if (include_timeout) {
    return folly::sformat("{}-{}", ap.toString(), server_timeout.count());
  } else {
    return ap.toString();
  }
}

}}}  // facebook::memcache::mcrouter
