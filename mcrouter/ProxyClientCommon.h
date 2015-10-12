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
#include <memory>
#include <string>

#include "mcrouter/lib/network/AccessPoint.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ClientPool;

struct ProxyClientCommon {
  const ClientPool& pool;
  std::shared_ptr<const AccessPoint> ap;

  const int keep_routing_prefix;
  const std::chrono::milliseconds server_timeout;

  const size_t indexInPool;

  const uint64_t qosClass;
  const uint64_t qosPath;

  const bool useSsl;
  const bool useTyped{false};

 private:
  ProxyClientCommon(const ClientPool& pool,
                    std::chrono::milliseconds timeout,
                    std::shared_ptr<const AccessPoint> ap,
                    int keep_routing_prefix,
                    uint64_t qosClass,
                    uint64_t qosPath,
                    bool useSsl,
                    bool useTyped);

  friend class ClientPool;
};

}}}  // facebook::memcache::mcrouter
