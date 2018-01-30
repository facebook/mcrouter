/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyDestinationMap.h"

#include <memory>

#include <folly/Format.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/network/AccessPoint.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace {

std::string genProxyDestinationKey(
    const AccessPoint& ap,
    std::chrono::milliseconds timeout) {
  if (ap.getProtocol() == mc_ascii_protocol) {
    // we cannot send requests with different timeouts for ASCII, since
    // it will break in-order nature of the protocol
    return folly::sformat("{}-{}", ap.toString(), timeout.count());
  } else {
    return ap.toString();
  }
}

} // anonymous

struct ProxyDestinationMap::StateList {
  using List =
      folly::IntrusiveList<ProxyDestination, &ProxyDestination::stateListHook_>;
  List list;
};

ProxyDestinationMap::ProxyDestinationMap(ProxyBase* proxy)
    : proxy_(proxy),
      active_(std::make_unique<StateList>()),
      inactive_(std::make_unique<StateList>()),
      inactivityTimeout_(0) {}

std::shared_ptr<ProxyDestination> ProxyDestinationMap::emplace(
    std::shared_ptr<AccessPoint> ap,
    std::chrono::milliseconds timeout,
    uint64_t qosClass,
    uint64_t qosPath,
    folly::StringPiece routerInfoName) {
  auto key = genProxyDestinationKey(*ap, timeout);
  auto destination = ProxyDestination::create(
      *proxy_, std::move(ap), timeout, qosClass, qosPath, routerInfoName);
  {
    std::lock_guard<std::mutex> lck(destinationsLock_);
    auto destIt = destinations_.emplace(key, destination);
    destination->pdstnKey_ = destIt.first->first;
  }

  // Update shared area of ProxyDestinations with same key from different
  // threads. This shared area is represented with TkoTracker class.
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
    const AccessPoint& ap,
    std::chrono::milliseconds timeout) const {
  auto key = genProxyDestinationKey(ap, timeout);
  {
    std::lock_guard<std::mutex> lck(destinationsLock_);
    return find(key);
  }
}

// Note: caller must be holding destionationsLock_.
std::shared_ptr<ProxyDestination> ProxyDestinationMap::find(
    const std::string& key) const {
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
  inactivityTimeout_ = static_cast<uint32_t>(interval.count());
  resetTimer_ =
      folly::AsyncTimeout::make(proxy_->eventBase(), [this]() noexcept {
        resetAllInactive();
        scheduleTimer(false /* initialAttempt */);
      });
  scheduleTimer(true /* initialAttempt */);
}

void ProxyDestinationMap::scheduleTimer(bool initialAttempt) {
  if (!resetTimer_->scheduleTimeout(inactivityTimeout_)) {
    MC_LOG_FAILURE(
        proxy_->router().opts(),
        memcache::failure::Category::kSystemError,
        "failed to {}schedule inactivity timer",
        initialAttempt ? "" : "re-");
  }
}

ProxyDestinationMap::~ProxyDestinationMap() {}

} // mcrouter
} // memcache
} // facebook
