/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ProxyClientCommon.h"

#include <folly/Conv.h>
#include <folly/Format.h>

#include "mcrouter/proxy.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyClientCommon::ProxyClientCommon(unsigned index,
                                     timeval_t timeout,
                                     AccessPoint ap_,
                                     int keep_routing_prefix_,
                                     bool attach_default_routing_prefix_,
                                     bool devnull_asynclog_,
                                     ProxyPool* pool_,
                                     std::string key,
                                     int rxpri,
                                     int txpri,
                                     size_t indexInPool_,
                                     bool useSsl_,
                                     uint64_t qos_)
    : pool(pool_),
      idx(index),
      ap(std::move(ap_)),
      proxy_client_key(std::move(key)),
      keep_routing_prefix(keep_routing_prefix_),
      attach_default_routing_prefix(attach_default_routing_prefix_),
      devnull_asynclog(devnull_asynclog_),
      rxpriority(rxpri),
      txpriority(txpri),
      server_timeout(std::move(timeout)),
      indexInPool(indexInPool_),
      useSsl(useSsl_),
      qos(qos_) {
  destination_key = folly::sformat("{}:{}", ap.getHost(), ap.getPort());
}

std::string ProxyClientCommon::genProxyDestinationKey() const {
  return ap.toString() + "-" + folly::to<std::string>(server_timeout.tv_sec)
         + "-" + folly::to<std::string>(server_timeout.tv_usec);
}

}}}  // facebook::memcache::mcrouter
