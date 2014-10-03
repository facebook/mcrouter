/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ProxyDestinationMap.h"

#include <folly/io/async/EventBase.h>
#include <folly/Memory.h>

#include "mcrouter/_router.h"
#include "mcrouter/lib/fbi/asox_timer.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

void onResetTimer(const asox_timer_t timer, void* arg) {
  auto map = reinterpret_cast<ProxyDestinationMap*>(arg);
  map->resetAllInactive();
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

void ProxyDestinationMap::removeAllUnused() {
  /* We don't want to destroy any ProxyDestinations while holding
     the destinationsLock_, since the destruction obtains other locks */
  std::vector<std::shared_ptr<ProxyDestination>> toRemove;

  {
    std::lock_guard<std::mutex> lck(destinationsLock_);

    auto cur = destinations_.begin();
    while (cur != destinations_.end()) {
      auto& dest = cur->second;
      if (dest.unique()) {
        if (dest->stateList_ == active_.get()) {
          active_->list.erase(StateList::List::s_iterator_to(*dest));
        } else if (dest->stateList_ == inactive_.get()) {
          inactive_->list.erase(StateList::List::s_iterator_to(*dest));
        }
        dest->stateList_ = nullptr;
        toRemove.emplace_back(std::move(dest));
        cur = destinations_.erase(cur);
      } else {
        ++cur;
      }
    }
  }
}

std::shared_ptr<ProxyDestination>
ProxyDestinationMap::fetch(const ProxyClientCommon& client) {
  auto key = client.genProxyDestinationKey();
  std::shared_ptr<ProxyDestination> destination;
  {
    std::lock_guard<std::mutex> lck(destinationsLock_);

    auto it = destinations_.find(key);
    if (it != destinations_.end()) {
      destination = it->second;
    } else {
      destination = ProxyDestination::create(proxy_, client, key);
      destinations_.emplace(std::move(key), destination);
    }
  }

  // Update shared area of ProxyDestinations with same key from different
  // threads. This shared area is represented with ProxyClientShared class.
  if (proxy_->router != nullptr) {
    proxy_->router->pclient_owner.updateProxyClientShared(
        *destination,
        proxy_->router->opts.failures_until_tko,
        proxy_->router->opts.maximum_soft_tkos,
        proxy_->router->current_soft_tkos);
  }

  return destination;
}

std::pair<uint64_t, uint64_t>
ProxyDestinationMap::getOutstandingRequestStats() {
  std::lock_guard<std::mutex> lck(destinationsLock_);
  uint64_t pending = 0;
  uint64_t inflight = 0;
  for (const auto& it : destinations_) {
    pending += it.second->getPendingRequestCount();
    inflight += it.second->getInflightRequestCount();
  }
  return std::make_pair(pending, inflight);
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
  resetTimer_ = asox_add_timer(proxy_->eventBase->getLibeventBase(), delay,
                               onResetTimer, this);
}

ProxyDestinationMap::~ProxyDestinationMap() {
  if (resetTimer_ != nullptr) {
    asox_remove_timer(resetTimer_);
  }
}

}}} // facebook::memcache::mcrouter
