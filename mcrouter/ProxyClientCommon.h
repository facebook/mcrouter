/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <sys/time.h>

#include <string>

#include "mcrouter/lib/network/AccessPoint.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyPool;

struct ProxyClientCommon {
  ProxyPool* pool;
  const AccessPoint ap;

  /// Always the same for a given (host, port)
  const std::string destination_key;

  const int keep_routing_prefix;
  const bool attach_default_routing_prefix;
  const timeval_t server_timeout;

  const size_t indexInPool;
  const bool useSsl;

  const uint64_t qos;

  std::string genProxyDestinationKey() const;

  ProxyClientCommon(timeval_t timeout,
                    AccessPoint ap,
                    int keep_routing_prefix,
                    bool attach_default_routing_prefix,
                    ProxyPool* pool,
                    bool useSsl,
                    uint64_t qos);
};

}}}  // facebook::memcache::mcrouter
