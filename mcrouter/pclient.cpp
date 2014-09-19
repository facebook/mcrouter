/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "pclient.h"

#include "mcrouter/ProxyDestination.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyClientShared::ProxyClientShared(const std::string& key_,
                                     const size_t tkoThreshold,
                                     const size_t maxSoftTkos,
                                     std::atomic<size_t>& currentSoftTkos,
                                     ProxyClientOwner& owner)
    : key(key_),
      tko(tkoThreshold, maxSoftTkos, currentSoftTkos),
      owner_(owner) {
}

ProxyClientShared::~ProxyClientShared() {
  std::lock_guard<std::mutex> guard(owner_.mx);
  auto it = owner_.pclient_shared.find(key);
  if (it != owner_.pclient_shared.end() && it->second.expired()) {
    owner_.pclient_shared.erase(it);
  }
}

void ProxyClientOwner::updateProxyClientShared(
  ProxyDestination& pdstn,
  const size_t tkoThreshold,
  const size_t maxSoftTkos,
  std::atomic<size_t>& currentSoftTkos) {
  const std::string& key = pdstn.destinationKey;
  {
    std::lock_guard<std::mutex> lock(mx);
    auto it = pclient_shared.find(key);
    std::shared_ptr<ProxyClientShared> pcs = nullptr;
    if (it == pclient_shared.end() || (pcs = it->second.lock()) == nullptr) {
      pcs = std::make_shared<ProxyClientShared>(key,
                                                tkoThreshold,
                                                maxSoftTkos,
                                                currentSoftTkos,
                                                *this);
      pclient_shared.emplace(key, pcs);
    }
    pcs->pdstns.insert(&pdstn);
    pdstn.shared = std::move(pcs);
  }
  pdstn.owner = this;
}

}}}
