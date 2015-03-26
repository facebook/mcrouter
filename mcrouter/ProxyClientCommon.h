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

#include <chrono>
#include <string>

#include "mcrouter/lib/network/AccessPoint.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ClientPool;

struct ProxyClientCommon {
  const ClientPool& pool;
  const AccessPoint ap;

  const int keep_routing_prefix;
  const std::chrono::milliseconds server_timeout;

  const size_t indexInPool;
  const bool useSsl;

  const uint64_t qos;

  std::string genProxyDestinationKey(bool include_timeout) const;

 private:
  ProxyClientCommon(const ClientPool& pool,
                    std::chrono::milliseconds timeout,
                    AccessPoint ap,
                    int keep_routing_prefix,
                    bool useSsl,
                    uint64_t qos);

  friend class ClientPool;
};

}}}  // facebook::memcache::mcrouter
