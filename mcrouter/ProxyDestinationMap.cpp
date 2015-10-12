/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyDestinationMap.h"

#include <folly/io/async/EventBase.h>
#include <folly/Format.h>
#include <folly/Memory.h>

#include "mcrouter/ClientPool.h"
#include "mcrouter/lib/fbi/asox_timer.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/network/AccessPoint.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestination.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

void onResetTimer(const asox_timer_t timer, void* arg) {
  auto map = reinterpret_cast<ProxyDestinationMap*>(arg);
  map->resetAllInactive();
}

std::string genProxyDestinationKey(const AccessPoint& ap,
                                   std::chrono::milliseconds timeout) {
  if (ap.getProtocol() == mc_ascii_protocol) {
    // we cannot send requests with different timeouts for ASCII, since
    // it will break in-order nature of the protocol
    return folly::sformat("{}-{}", ap.toString(), timeout.count());
  } else {
    return ap.toString();
  }
}

} // namespace

struct ProxyDestinationMap::StateList {
  using List = folly::IntrusiveList<ProxyDestination,
                                    &ProxyDestination::stateListHook_>;
  List list;
};

ProxyDestinationMap::ProxyDestinationMap(proxy_t* proxy)
  : proxy_(proxy),
    active_(folly::make_unique<StateList>()),
    inactive_(folly::make_unique<StateList>()),
    resetTimer_(nullptr) {
}

std::shared_ptr<ProxyDestination>
ProxyDestinationMap::fetch(const ProxyClientCommon& client) {
  auto key = genProxyDestinationKey(*client.ap, client.server_timeout);
  std::shared_ptr<ProxyDestination> destination;
  {
    std::lock_guard<std::mutex> lck(destinationsLock_);

    destination = find(key);
    if (!destination) {
      destination = ProxyDestination::create(proxy_, client);
      auto destIt = destinations_.emplace(key, destination);
      if (!destIt.second) {
        destIt.first->second = destination;
      }
      destination->pdstnKey_ = destIt.first->first;
    } else {
      destination->updatePoolName(client.pool.getName());
      destination->updateShortestTimeout(client.server_timeout);
    }
  }

  // Update shared area of ProxyDestinations with same key from different
  // threads. This shared area is represented with ProxyClientShared class.
  proxy_->router().tkoTrackerMap().updateTracker(
    *destination,
    proxy_->router().opts().failures_until_tko,
    proxy_->router().opts().maximum_soft_tkos);

  return destination;
}

/**
 * If ProxyDestination is already stored in this object - returns it;
 * otherwise, returns nullptr.
 */
std::shared_ptr<ProxyDestination> ProxyDestinationMap::find(
    const AccessPoint& ap, std::chrono::milliseconds timeout) const {
  auto key = genProxyDestinationKey(ap, timeout);
  {
    std::lock_guard<std::mutex> lck(destinationsLock_);
    return find(key);
  }
}

// Note: caller must be holding destionationsLock_.
std::shared_ptr<ProxyDestination>
ProxyDestinationMap::find(const std::string& key) const {
  auto it = destinations_.find(key);
  if (it == destinations_.end()) {
    return nullptr;
  }
  return it->second.lock();
}

void ProxyDestinationMap::removeDestination(ProxyDestination& destination) {
  if (destination.stateList_ == active_.get()) {
    active_->list.erase(StateList::List::s_iterator_to(destination));
  } else if (destination.stateList_ == inactive_.get()) {
    inactive_->list.erase(StateList::List::s_iterator_to(destination));
  }
  {
    std::lock_guard<std::mutex> lck(destinationsLock_);
    destinations_.erase(destination.pdstnKey_);
  }
}

void ProxyDestinationMap::markAsActive(ProxyDestination& destination) {
  if (destination.stateList_ == active_.get()) {
    return;
  }
  if (destination.stateList_ == inactive_.get()) {
    inactive_->list.erase(StateList::List::s_iterator_to(destination));
  }
  active_->list.push_back(destination);
  destination.stateList_ = active_.get();
}

void ProxyDestinationMap::resetAllInactive() {
  for (auto& it : inactive_->list) {
    it.resetInactive();
    it.stateList_ = nullptr;
  }
  inactive_->list.clear();
  active_.swap(inactive_);
}

void ProxyDestinationMap::setResetTimer(std::chrono::milliseconds interval) {
  assert(interval.count() > 0);
  auto delay = to<timeval_t>((unsigned int)interval.count());
  resetTimer_ = asox_add_timer(proxy_->eventBase().getLibeventBase(), delay,
                               onResetTimer, this);
}

ProxyDestinationMap::~ProxyDestinationMap() {
  if (resetTimer_ != nullptr) {
    asox_remove_timer(resetTimer_);
  }
}

}}} // facebook::memcache::mcrouter
