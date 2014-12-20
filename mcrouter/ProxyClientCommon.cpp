/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ProxyClientCommon.h"

#include <folly/Format.h>

#include "mcrouter/proxy.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyClientCommon::ProxyClientCommon(timeval_t timeout,
                                     AccessPoint ap_,
                                     int keep_routing_prefix_,
                                     bool attach_default_routing_prefix_,
                                     ProxyPool* pool_,
                                     bool useSsl_,
                                     uint64_t qos_)
    : pool(pool_),
      ap(std::move(ap_)),
      destination_key(folly::sformat("{}:{}", ap.getHost(), ap.getPort())),
      keep_routing_prefix(keep_routing_prefix_),
      attach_default_routing_prefix(attach_default_routing_prefix_),
      server_timeout(std::move(timeout)),
      indexInPool(pool->clients.size()),
      useSsl(useSsl_),
      qos(qos_) {
}

std::string ProxyClientCommon::genProxyDestinationKey() const {
  return folly::sformat("{}-{}", ap.toString(),
                        to<std::chrono::milliseconds>(server_timeout).count());
}

}}}  // facebook::memcache::mcrouter
