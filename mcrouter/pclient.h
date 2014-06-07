/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "mcrouter/TkoTracker.h"

namespace facebook { namespace memcache { namespace mcrouter {

class ProxyClientOwner;
class ProxyDestination;

/**
 * Proxy clients from multiple proxy threads can share this storage.
 * If pclient->shared is not null, it points to valid storage area
 * that's shared for all the clients with the same destination_key
 */
struct ProxyClientShared {
  /// "host:port" uniquely identifying this shared object
  std::string key;
  TkoTracker tko;

  /// pclients that reference this shared object
  std::unordered_set<ProxyDestination*> pclients;

  ProxyClientShared(const std::string& key_,
                    const size_t tkoThreshold,
                    const size_t maxSoftTkos,
                    std::atomic<size_t>& currentSoftTkos,
                    ProxyClientOwner& owner);

  ~ProxyClientShared();

 private:
  ProxyClientOwner& owner_;
};

/**
 * Manages the lifetime of proxy clients and their shared areas.
 */
struct ProxyClientOwner {
  /**
   * Creates/updates ProxyClientShared with the given pclient
   * and also updates pclient->shared pointer.
   */
  void updateProxyClientShared(ProxyDestination& pdstn,
                               const size_t tkoThreshold,
                               const size_t maxSoftTkos,
                               std::atomic<size_t>& currentSoftTkos);
  /**
   * Calls func(key, ProxyClientShared*) for each live proxy client
   * shared object.  The whole map will be locked for the duration of the call.
   */
  template<typename F>
  void foreach_shared_synchronized(const F& func);

 private:
  std::mutex mx;
  std::unordered_map<std::string, std::weak_ptr<ProxyClientShared>>
    pclient_shared;

  friend class ProxyDestination;

  friend ProxyClientShared::~ProxyClientShared();
};

}}}

#include "pclient-inl.h"
